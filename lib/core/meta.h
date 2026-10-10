#pragma once

#include "component_base.h"
#include "cstdmf/hash_literal.h"
#include "cstdmf/static_vector.h"
#include "archive.h"
#include "serialization_context.h"

#include <cstdint>
#include <string_view>
#include <type_traits>
#include <unordered_map>

using namespace literal;

namespace csyren::core::reflection
{
    using LiteralID = cstdmf::LiteralID;

    enum class FieldFlags : uint32_t { None = 0, Serializable = 1 << 0, EditorVisible = 1 << 1, Transient = 1 << 2, ReadOnly = 1 << 3, Required = 1 << 4 };
    constexpr FieldFlags operator|(FieldFlags a, FieldFlags b) noexcept { return static_cast<FieldFlags>(static_cast<uint32_t>(a) | static_cast<uint32_t>(b)); }
    constexpr bool hasFlag(FieldFlags value, FieldFlags flag) noexcept { return (static_cast<uint32_t>(value) & static_cast<uint32_t>(flag)) != 0; }

    struct MetaType;
    struct MetaArchiveOps
    {
        using Save = bool (*)(ArchiveWriter&, SerializationContext&, const MetaType&, const void*);
        using Load = bool (*)(ArchiveReader&, SerializationContext&, const MetaType&, void*);
        Save save{};
        Load load{};
    };
    bool saveFields(ArchiveWriter&, SerializationContext&, const MetaType&, const void*);
    bool loadFields(ArchiveReader&, SerializationContext&, const MetaType&, void*);
    struct MetaData
    {
        using GetFn = void* (*)(void*) noexcept;
        using GetConstFn = const void* (*)(const void*) noexcept;
        LiteralID id{ 0 };                 // Stable ID for compact/binary formats.
        std::string_view name{};           // Human-readable JSON/editor name.
        size_t size{ 0 };
        MetaType* type{ nullptr };
        FieldFlags flags{ FieldFlags::Serializable | FieldFlags::EditorVisible };
        GetFn get{ nullptr };
        GetConstFn getConst{ nullptr };
    };

    struct MetaType
    {
        static constexpr size_t MAX_FIELD_COUNT = 32;
        LiteralID id{ 0 };
        std::string_view name{};
        size_t size{ 0 };
        uint32_t version{ 1 };
        MetaArchiveOps archive{};
        cstdmf::StaticVector<MetaData, MAX_FIELD_COUNT> data;
    };
}

namespace csyren::core::reflection::details
{
    template<class Type> class MetaLink { public: static inline MetaType type{}; };

    class MetaContext
    {
        using MetaMap = std::unordered_map<ComponentFamily::typeID, MetaType*>;
    public:
        static MetaContext& instance() { static MetaContext context; return context; }
        MetaType* get(LiteralID id) const { auto it = _types.find(id); return it != _types.end() ? it->second : nullptr; }
        MetaType* get(ComponentFamily::typeID id) const { auto it = _typesByCompID.find(id); return it != _typesByCompID.end() ? it->second : nullptr; }
        MetaType* insert(LiteralID id, std::string_view name, ComponentFamily::typeID componentId, size_t size, MetaType* type)
        {
            if (auto it = _types.find(id); it != _types.end()) return it->second;
            type->id = id; type->name = name; type->size = size;
            _types.emplace(id, type); _typesByCompID.emplace(componentId, type); return type;
        }
        MetaType* insert(LiteralID id, std::string_view name, size_t size, MetaType* type)
        {
            if (auto it = _types.find(id); it != _types.end()) return it->second;
            type->id = id; type->name = name; type->size = size;
            _types.emplace(id, type); return type;
        }
        struct MetaView
        {
            using iterator = MetaMap::const_iterator;
            MetaView(iterator begin, iterator end) noexcept : _begin(begin), _end(end) {}
            iterator begin() const noexcept { return _begin; }
            iterator end() const noexcept { return _end; }
        private: iterator _begin, _end;
        };
        MetaView view() const { return { _typesByCompID.begin(), _typesByCompID.end() }; }
    private:
        std::unordered_map<LiteralID, MetaType*, cstdmf::LiteralIDHash> _types;
        MetaMap _typesByCompID;
    };
    template<typename> struct MemberType;
    template<typename T, typename M> struct MemberType<M T::*> { using type = M; };
    template<typename T> using MemberType_t = typename MemberType<T>::type;
    template<typename> struct MemberOwner;
    template<typename Owner, typename Member> struct MemberOwner<Member Owner::*> { using type = Owner; };
}

namespace csyren::core::reflection
{
    template<class Type> class MetaFactory
    {
    public:
        MetaFactory& type(LiteralID id, std::string_view name = {})
        {
            _type = details::MetaContext::instance().insert(id, name.empty() ? id.str : name, ComponentFamily::getID<Type>(), sizeof(Type), &details::MetaLink<Type>::type);
            if (!_type->archive.save) _type->archive = { &saveFields, &loadFields };
            return *this;
        }
        MetaFactory& primitiveType(LiteralID id, std::string_view name = {})
        {
            _type = details::MetaContext::instance().insert(id, name.empty() ? id.str : name, sizeof(Type), &details::MetaLink<Type>::type); return *this;
        }
        MetaFactory& version(uint32_t value) { _type->version = value; return *this; }
        MetaFactory& archive(MetaArchiveOps ops) { _type->archive = ops; return *this; }
        template<auto Member> MetaFactory& data(LiteralID id, std::string_view name = {}, FieldFlags flags = FieldFlags::Serializable | FieldFlags::EditorVisible)
        {
            static_assert(!std::is_member_function_pointer_v<decltype(Member)>);
            using Owner = typename details::MemberOwner<decltype(Member)>::type;
            using MemberType = details::MemberType_t<decltype(Member)>;
            for (const auto& existing : _type->data) if (existing.id == id) return *this;
            MetaData field;
            field.id = id; field.name = name.empty() ? id.str : name; field.size = sizeof(MemberType);
            field.type = &details::MetaLink<std::remove_cvref_t<MemberType>>::type; field.flags = flags;
            field.get = [](void* object) noexcept -> void* { return &(static_cast<Owner*>(object)->*Member); };
            field.getConst = [](const void* object) noexcept -> const void* { return &(static_cast<const Owner*>(object)->*Member); };
            _type->data.emplace_back(field); return *this;
        }
    private: MetaType* _type{ nullptr };
    };
    inline MetaType* resolve(LiteralID id) { return details::MetaContext::instance().get(id); }
    inline MetaType* resolve(ComponentFamily::typeID id) { return details::MetaContext::instance().get(id); }
    template<class Type> inline MetaType* resolve() noexcept { return &details::MetaLink<std::remove_cvref_t<Type>>::type; }
    inline auto resolveAll() { return details::MetaContext::instance().view(); }
    inline bool save(ArchiveWriter& writer, SerializationContext& context, const MetaType& type, const void* value)
    {
        return value && type.archive.save && type.archive.save(writer, context, type, value);
    }
    inline bool load(ArchiveReader& reader, SerializationContext& context, const MetaType& type, void* value)
    {
        return value && type.archive.load && type.archive.load(reader, context, type, value);
    }
    inline bool saveFields(ArchiveWriter& writer, SerializationContext& context, const MetaType& type, const void* value)
    {
        if (!writer.object()) return false;
        for (const auto& field : type.data)
        {
            if (!hasFlag(field.flags, FieldFlags::Serializable) || hasFlag(field.flags, FieldFlags::Transient)) continue;
            if (!field.type || !field.getConst || !writer.beginField(field.id.id, field.name)) return false;
            const bool ok = save(writer, context, *field.type, field.getConst(value));
            if (!writer.endField() || !ok) return false;
        }
        return writer.endObject();
    }
    inline bool loadFields(ArchiveReader& reader, SerializationContext& context, const MetaType& type, void* value)
    {
        uint32_t version{};
        if (!reader.object(version) || version > type.version) return false;
        for (const auto& field : type.data)
        {
            if (!hasFlag(field.flags, FieldFlags::Serializable) || hasFlag(field.flags, FieldFlags::Transient)) continue;
            bool present{};
            if (!reader.beginField(field.id.id, field.name, present)) return false;
            if (!present) { if (hasFlag(field.flags, FieldFlags::Required)) return false; continue; }
            const bool ok = field.type && field.get && load(reader, context, *field.type, field.get(value));
            if (!reader.endField() || !ok) return false;
        }
        return reader.endObject();
    }
}

namespace csyren::core::reflection::details { template<class Type> void tryDescribe() { if constexpr (requires { Type::describe(); }) Type::describe(); } }
#define REFLECT(Type) ::csyren::core::reflection::details::tryDescribe<Type>();
