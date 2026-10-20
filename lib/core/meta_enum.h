#pragma once
// Reflection for enums. Included at the end of meta.h; use CS_ENUM / MaskOf
// from reflected_enum.h in component headers.
//
// * CS_ENUM enums and MaskOf<E> are registered automatically when a reflected
//   field has that type (MetaFactory::data). registerEnum<E>() registers one
//   explicitly (e.g. an enum that is only used from code).
// * Text archives store names ("Dynamic", ["Player", "Projectile"]) and still
//   accept the old integer form. Compact archives (ArchiveWriter::compact())
//   store name hashes, so reordering enumerators never changes saved data.
// * Unknown names / hashes keep the field's default and log a warning.
// * registerNamedEnum<T>() reflects a value type that stores a *name* chosen
//   from a runtime list (e.g. physics::CollisionLayer from the layer registry).
#include "meta.h"
#include "reflected_enum.h"
#include "cstdmf/log.h"

#include <optional>
#include <string>
#include <vector>

namespace csyren::core::reflection
{
    constexpr uint32_t enumNameHash(std::string_view name) noexcept
    {
        uint32_t hash = 0x811c9dc5u; // FNV-1a, same function as field ids (low 32 bits)
        for (char c : name) { hash ^= static_cast<uint8_t>(c); hash *= 0x01000193u; }
        return hash;
    }

    struct MetaEnumEntry
    {
        std::string name;
        int64_t value = 0;
        uint32_t hash = 0;
    };

    struct MetaEnum
    {
        enum class Kind : uint8_t { Value, Mask };
        Kind kind = Kind::Value;
        std::string_view typeName;
        std::vector<MetaEnumEntry> entries;     // declaration order
        std::vector<MetaEnumEntry> aliases;     // read-only alternative names
        // Runtime entry list (named enums); replaces `entries` when set.
        std::vector<MetaEnumEntry> (*provider)() = nullptr;
        // Integer storage: enum value, or the bit set of a mask.
        int64_t (*getValue)(const void*) = nullptr;
        void (*setValue)(void*, int64_t) = nullptr;
        // Name storage (named enums): unknown names are kept as they are.
        std::string (*getName)(const void*) = nullptr;
        void (*setName)(void*, std::string_view) = nullptr;

        bool isMask() const noexcept { return kind == Kind::Mask; }
        bool storesName() const noexcept { return getName && setName; }
        std::vector<MetaEnumEntry> list() const { return provider ? provider() : entries; }

        std::optional<MetaEnumEntry> byName(std::string_view name) const
        {
            for (auto& entry : list()) if (entry.name == name) return entry;
            for (auto& entry : aliases) if (entry.name == name) return entry;
            return std::nullopt;
        }
        std::optional<MetaEnumEntry> byValue(int64_t value) const
        {
            for (auto& entry : list()) if (entry.value == value) return entry;
            return std::nullopt;
        }
        std::optional<MetaEnumEntry> byHash(uint32_t hash) const
        {
            for (auto& entry : list()) if (entry.hash == hash) return entry;
            for (auto& entry : aliases) if (entry.hash == hash) return entry;
            return std::nullopt;
        }

        // ---- editor helpers --------------------------------------------------
        uint64_t allBits() const
        {
            uint64_t bits = 0;
            for (auto& entry : list()) if (entry.value >= 0 && entry.value < 64) bits |= uint64_t(1) << entry.value;
            return bits;
        }
        static uint64_t bitOf(const MetaEnumEntry& entry) noexcept
        {
            return entry.value >= 0 && entry.value < 64 ? uint64_t(1) << entry.value : 0;
        }
        bool isSelected(const void* value, const MetaEnumEntry& entry) const
        {
            if (isMask()) return (static_cast<uint64_t>(getValue(value)) & bitOf(entry)) != 0;
            if (storesName()) return getName(value) == entry.name;
            return getValue(value) == entry.value;
        }
        void select(void* value, const MetaEnumEntry& entry) const
        {
            if (storesName()) setName(value, entry.name);
            else setValue(value, entry.value);
        }
        void setFlag(void* value, const MetaEnumEntry& entry, bool on) const
        {
            auto bits = static_cast<uint64_t>(getValue(value));
            bits = on ? (bits | bitOf(entry)) : (bits & ~bitOf(entry));
            setValue(value, static_cast<int64_t>(bits));
        }
        std::string label(const void* value) const
        {
            if (isMask())
            {
                const auto bits = static_cast<uint64_t>(getValue(value));
                if (bits == 0) return "None";
                if (bits == allBits()) return "All";
                std::string text;
                for (auto& entry : list())
                    if (bits & bitOf(entry)) { if (!text.empty()) text += ", "; text += entry.name; }
                return text;
            }
            if (storesName())
            {
                auto name = getName(value);
                return byName(name) ? name : name + " (unknown)";
            }
            const auto current = getValue(value);
            if (auto entry = byValue(current)) return entry->name;
            return "#" + std::to_string(current);
        }
    };

    // Shared archive operations for every enum / mask / named enum type.
    inline bool saveEnum(ArchiveWriter& writer, SerializationContext&, const MetaType& type, const void* value)
    {
        const auto* info = type.enumInfo;
        if (!info) return false;
        if (info->isMask())
        {
            const auto bits = static_cast<uint64_t>(info->getValue(value));
            std::vector<MetaEnumEntry> selected;
            for (auto& entry : info->list()) if (bits & MetaEnum::bitOf(entry)) selected.push_back(entry);
            if (!writer.sequence(selected.size())) return false;
            for (size_t i = 0; i < selected.size(); ++i)
            {
                if (!writer.beginElement(i)) return false;
                const bool ok = writer.compact() ? writer.unsignedInteger(selected[i].hash) : writer.string(selected[i].name);
                if (!writer.endElement() || !ok) return false;
            }
            return writer.endSequence();
        }
        if (info->storesName()) return writer.string(info->getName(value));
        const auto current = info->getValue(value);
        const auto entry = info->byValue(current);
        if (writer.compact()) return writer.unsignedInteger(entry ? entry->hash : 0);
        return entry ? writer.string(entry->name) : writer.signedInteger(current);
    }

    inline bool loadEnum(ArchiveReader& reader, SerializationContext&, const MetaType& type, void* value)
    {
        const auto* info = type.enumInfo;
        if (!info) return false;
        const auto unknown = [&](const std::string& what) {
            log::warning("Reflection: unknown {} {} in '{}', keeping the default", info->isMask() ? "element" : "value", what, info->typeName);
        };
        if (info->isMask())
        {
            size_t count{};
            if (!reader.sequence(count))
            {
                uint64_t legacy{}; // old integer bit set
                if (reader.compact() || !reader.unsignedInteger(legacy)) return false;
                info->setValue(value, static_cast<int64_t>(legacy & info->allBits()));
                return true;
            }
            uint64_t bits = 0;
            for (size_t i = 0; i < count; ++i)
            {
                if (!reader.beginElement(i)) return false;
                std::optional<MetaEnumEntry> entry;
                std::string name; uint64_t hash{};
                if (reader.compact()) { if (!reader.unsignedInteger(hash)) return false; entry = info->byHash(static_cast<uint32_t>(hash)); if (!entry) unknown("#" + std::to_string(hash)); }
                else { if (!reader.string(name)) return false; entry = info->byName(name); if (!entry) unknown("'" + name + "'"); }
                if (entry) bits |= MetaEnum::bitOf(*entry);
                if (!reader.endElement()) return false;
            }
            if (!reader.endSequence()) return false;
            info->setValue(value, static_cast<int64_t>(bits));
            return true;
        }
        if (info->storesName())
        {
            std::string name;
            if (!reader.string(name))
            {
                int64_t legacy{}; // numeric value from an older format: keep the default name
                if (reader.compact() || !reader.signedInteger(legacy)) return false;
                unknown(std::to_string(legacy));
                return true;
            }
            if (!info->byName(name)) unknown("'" + name + "'"); // kept as is, written back unchanged
            info->setName(value, name);
            return true;
        }
        if (reader.compact())
        {
            uint64_t hash{};
            if (!reader.unsignedInteger(hash)) return false;
            if (auto entry = info->byHash(static_cast<uint32_t>(hash))) info->setValue(value, entry->value);
            else unknown("#" + std::to_string(hash));
            return true;
        }
        std::string name;
        if (reader.string(name))
        {
            if (auto entry = info->byName(name)) info->setValue(value, entry->value);
            else unknown("'" + name + "'");
            return true;
        }
        int64_t legacy{}; // scenes written before enums were reflected by name
        if (!reader.signedInteger(legacy)) return false;
        if (info->byValue(legacy)) info->setValue(value, legacy);
        else unknown(std::to_string(legacy));
        return true;
    }

    inline MetaArchiveOps enumArchive() { return { &saveEnum, &loadEnum }; }

    namespace details
    {
        template<class T> MetaEnum& enumInfoStorage() { static MetaEnum info; return info; }
        // `name` must be NUL-terminated and outlive the program (literal / static string).
        inline bool insertEnumType(std::string_view name, size_t size, MetaType* type, const MetaEnum* info)
        {
            const auto id = cstdmf::fnv1a_32(name.data(), name.size());
            auto* registered = MetaContext::instance().insert(id, name, size, type);
            if (registered != type)
            {
                log::error("Reflection: type name '{}' is already used by another type; enum not registered", name);
                return false;
            }
            type->archive = enumArchive();
            type->enumInfo = info;
            return true;
        }
    }

    template<enums::ReflectedEnum E> MetaType* registerMask();

    template<enums::ReflectedEnum E> MetaType* registerEnum()
    {
        auto* type = &details::MetaLink<E>::type;
        static bool attempted = false;
        if (attempted) return type;
        attempted = true;
        using Traits = enums::EnumTraits<E>;
        auto& info = details::enumInfoStorage<E>();
        info.kind = MetaEnum::Kind::Value;
        info.typeName = Traits::typeName;
        info.entries.clear();
        for (size_t i = 0; i < Traits::count; ++i)
            info.entries.push_back({ std::string(Traits::names[i]), static_cast<int64_t>(i), enumNameHash(Traits::names[i]) });
        info.aliases.clear();
        for (const auto& [alias, aliasValue] : enums::enumAliases<E>())
            info.aliases.push_back({ std::string(alias), static_cast<int64_t>(enums::enumIndex(aliasValue)), enumNameHash(alias) });
        info.getValue = [](const void* p) -> int64_t { return static_cast<int64_t>(enums::enumIndex(*static_cast<const E*>(p))); };
        info.setValue = [](void* p, int64_t v) { *static_cast<E*>(p) = static_cast<E>(v); };
        if (details::insertEnumType(Traits::typeName, sizeof(E), type, &info)) registerMask<E>();
        return type;
    }

    template<enums::ReflectedEnum E> MetaType* registerMask()
    {
        using Mask = enums::MaskOf<E>;
        auto* type = &details::MetaLink<Mask>::type;
        static bool attempted = false;
        if (attempted) return type;
        attempted = true;
        auto* element = registerEnum<E>();
        if (!element->enumInfo) return type;
        auto& info = details::enumInfoStorage<Mask>();
        info = *element->enumInfo;
        info.kind = MetaEnum::Kind::Mask;
        info.getValue = [](const void* p) -> int64_t { return static_cast<int64_t>(static_cast<const Mask*>(p)->bits()); };
        info.setValue = [](void* p, int64_t v) { *static_cast<Mask*>(p) = Mask::fromBits(static_cast<uint64_t>(v)); };
        static const std::string name = "MaskOf<" + std::string(enums::EnumTraits<E>::typeName) + ">";
        details::insertEnumType(name, sizeof(Mask), type, &info);
        return type;
    }

    // A value type holding a name picked from a runtime list. `T` must expose
    // `std::string name`. The list provider is only called by tools (editor);
    // saving/loading only reads and writes the name.
    template<class T> MetaType* registerNamedEnum(std::string_view typeName, std::vector<MetaEnumEntry> (*provider)())
    {
        auto* type = &details::MetaLink<T>::type;
        static bool attempted = false;
        if (attempted) return type;
        attempted = true;
        auto& info = details::enumInfoStorage<T>();
        info.kind = MetaEnum::Kind::Value;
        info.typeName = typeName;
        info.provider = provider;
        info.getName = [](const void* p) -> std::string { return static_cast<const T*>(p)->name; };
        info.setName = [](void* p, std::string_view name) { static_cast<T*>(p)->name = std::string(name); };
        details::insertEnumType(typeName, sizeof(T), type, &info);
        return type;
    }

    // Helper for providers: entries from a list of names (value = position).
    inline std::vector<MetaEnumEntry> enumEntries(const std::vector<std::string>& names)
    {
        std::vector<MetaEnumEntry> entries;
        entries.reserve(names.size());
        for (size_t i = 0; i < names.size(); ++i) entries.push_back({ names[i], static_cast<int64_t>(i), enumNameHash(names[i]) });
        return entries;
    }
}

namespace csyren::core::reflection::details
{
    template<class T> void autoRegister()
    {
        if constexpr (enums::ReflectedEnum<T>) registerEnum<T>();
        else if constexpr (enums::MaskOfEnum<T>) registerMask<typename T::enum_type>();
    }
}
