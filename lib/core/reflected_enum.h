#pragma once
// Reflected enums without a reflection dependency (safe for any header).
//
//   CS_ENUM(BodyType, uint8_t, Static, Kinematic, Dynamic)
//
// declares `enum class BodyType : uint8_t { Static, Kinematic, Dynamic }` plus a
// name table the engine finds by ADL. Rules: namespace scope only, enumerators
// are sequential (no "= value"), at most 64 of them if used with MaskOf.
// The reflection system registers the enum (and MaskOf<E>) automatically when
// a reflected field has that type (core/meta_enum.h); names are serialized,
// old integer values still load.
//
//   MaskOf<BodyType> mask{ BodyType::Static, BodyType::Dynamic };
//
// a set of enum values (bit i = value i), serialized as a list of names.
//
// Renames: CS_ENUM_ALIAS(BodyType, "Kinematik", Kinematic) in a .cpp keeps old
// files loading (aliases are read-only, the current name is written).
#include <array>
#include <concepts>
#include <cstdint>
#include <initializer_list>
#include <optional>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

namespace csyren::core::enums
{
    namespace details
    {
        constexpr bool isSpace(char c) noexcept { return c == ' ' || c == '\t' || c == '\n' || c == '\r'; }
        constexpr std::string_view trim(std::string_view text) noexcept
        {
            while (!text.empty() && isSpace(text.front())) text.remove_prefix(1);
            while (!text.empty() && isSpace(text.back())) text.remove_suffix(1);
            return text;
        }
        // Number of comma separated entries (a trailing comma is allowed).
        constexpr size_t countEntries(std::string_view list) noexcept
        {
            size_t count = 0;
            while (true)
            {
                const auto comma = list.find(',');
                const auto entry = trim(list.substr(0, comma));
                if (!entry.empty()) ++count;
                if (comma == std::string_view::npos) break;
                list.remove_prefix(comma + 1);
            }
            return count;
        }
        template<size_t N> constexpr std::array<std::string_view, N> splitEntries(std::string_view list) noexcept
        {
            std::array<std::string_view, N> result{};
            size_t index = 0;
            while (index < N)
            {
                const auto comma = list.find(',');
                const auto entry = trim(list.substr(0, comma));
                if (!entry.empty()) result[index++] = entry;
                if (comma == std::string_view::npos) break;
                list.remove_prefix(comma + 1);
            }
            return result;
        }
        constexpr bool hasExplicitValues(std::string_view list) noexcept { return list.find('=') != std::string_view::npos; }
    }

    // An enum declared with CS_ENUM.
    template<class E>
    concept ReflectedEnum = std::is_enum_v<E> && requires(E value) {
        { csyrenEnumSource(value) } -> std::convertible_to<std::string_view>;
        { csyrenEnumTypeName(value) } -> std::convertible_to<std::string_view>;
    };

    template<ReflectedEnum E> struct EnumTraits
    {
        using underlying = std::underlying_type_t<E>;
        static constexpr std::string_view source = csyrenEnumSource(E{});
        static constexpr std::string_view typeName = csyrenEnumTypeName(E{});
        static constexpr size_t count = details::countEntries(source);
        static constexpr std::array<std::string_view, count> names = details::splitEntries<count>(source);
        static_assert(!details::hasExplicitValues(source), "CS_ENUM enumerators must be sequential (no '= value')");
    };

    template<ReflectedEnum E> constexpr size_t enumCount() noexcept { return EnumTraits<E>::count; }
    template<ReflectedEnum E> constexpr size_t enumIndex(E value) noexcept { return static_cast<size_t>(static_cast<std::make_unsigned_t<typename EnumTraits<E>::underlying>>(value)); }
    template<ReflectedEnum E> constexpr std::string_view enumTypeName() noexcept { return EnumTraits<E>::typeName; }
    // Empty for values outside the declared range.
    template<ReflectedEnum E> constexpr std::string_view enumName(E value) noexcept
    {
        const auto index = enumIndex(value);
        return index < EnumTraits<E>::count ? EnumTraits<E>::names[index] : std::string_view{};
    }
    template<ReflectedEnum E> constexpr std::array<E, EnumTraits<E>::count> enumValues() noexcept
    {
        std::array<E, EnumTraits<E>::count> values{};
        for (size_t i = 0; i < values.size(); ++i) values[i] = static_cast<E>(i);
        return values;
    }

    // Read-only alternative names (see CS_ENUM_ALIAS).
    template<ReflectedEnum E> std::vector<std::pair<std::string_view, E>>& enumAliases()
    {
        static std::vector<std::pair<std::string_view, E>> aliases;
        return aliases;
    }
    template<ReflectedEnum E> struct AliasRegistrar
    {
        AliasRegistrar(std::string_view name, E value) { enumAliases<E>().emplace_back(name, value); }
    };

    // Declared names first, then aliases.
    template<ReflectedEnum E> std::optional<E> enumFromName(std::string_view name) noexcept
    {
        for (size_t i = 0; i < EnumTraits<E>::count; ++i)
            if (EnumTraits<E>::names[i] == name) return static_cast<E>(i);
        for (const auto& [alias, value] : enumAliases<E>())
            if (alias == name) return value;
        return std::nullopt;
    }

    // Set of values of a reflected enum. Bit i stands for the enumerator with value i.
    template<ReflectedEnum E> class MaskOf
    {
        static_assert(EnumTraits<E>::count <= 64, "MaskOf supports enums with at most 64 enumerators");
    public:
        using enum_type = E;
        constexpr MaskOf() noexcept = default;
        constexpr MaskOf(std::initializer_list<E> values) noexcept { for (auto value : values) set(value); }

        static constexpr MaskOf none() noexcept { return {}; }
        static constexpr MaskOf all() noexcept { return fromBits(allBits()); }
        static constexpr MaskOf fromBits(uint64_t bits) noexcept { MaskOf mask; mask._bits = bits & allBits(); return mask; }
        static constexpr uint64_t allBits() noexcept
        {
            return EnumTraits<E>::count >= 64 ? ~uint64_t(0) : (uint64_t(1) << EnumTraits<E>::count) - 1;
        }
        static constexpr uint64_t bit(E value) noexcept
        {
            const auto index = enumIndex(value);
            return index < 64 ? uint64_t(1) << index : 0;
        }

        constexpr uint64_t bits() const noexcept { return _bits; }
        constexpr bool test(E value) const noexcept { return (_bits & bit(value)) != 0; }
        constexpr bool empty() const noexcept { return _bits == 0; }
        constexpr bool any() const noexcept { return _bits != 0; }
        constexpr MaskOf& set(E value, bool on = true) noexcept { _bits = on ? (_bits | bit(value)) : (_bits & ~bit(value)); return *this; }
        constexpr MaskOf& reset(E value) noexcept { return set(value, false); }

        constexpr MaskOf operator|(MaskOf other) const noexcept { return fromBits(_bits | other._bits); }
        constexpr MaskOf operator&(MaskOf other) const noexcept { return fromBits(_bits & other._bits); }
        constexpr MaskOf operator-(MaskOf other) const noexcept { return fromBits(_bits & ~other._bits); }
        constexpr MaskOf operator~() const noexcept { return fromBits(~_bits); }
        constexpr MaskOf operator|(E value) const noexcept { return fromBits(_bits | bit(value)); }
        constexpr MaskOf operator-(E value) const noexcept { return fromBits(_bits & ~bit(value)); }
        constexpr MaskOf& operator|=(MaskOf other) noexcept { _bits |= other._bits; return *this; }
        constexpr MaskOf& operator&=(MaskOf other) noexcept { _bits &= other._bits; return *this; }
        constexpr MaskOf& operator-=(MaskOf other) noexcept { _bits &= ~other._bits; return *this; }
        constexpr bool operator==(const MaskOf&) const noexcept = default;

        template<class Fn> constexpr void forEach(Fn&& fn) const
        {
            for (size_t i = 0; i < EnumTraits<E>::count; ++i)
                if (_bits & (uint64_t(1) << i)) fn(static_cast<E>(i));
        }
    private:
        uint64_t _bits = 0;
    };

    template<class T> struct IsMaskOf : std::false_type {};
    template<class E> struct IsMaskOf<MaskOf<E>> : std::true_type {};
    template<class T> concept MaskOfEnum = IsMaskOf<std::remove_cvref_t<T>>::value;
}

namespace csyren::core
{
    using enums::MaskOf;
    using enums::enumName;
    using enums::enumFromName;
    using enums::enumValues;
    using enums::enumCount;
}

#define CS_ENUM(Name, Underlying, ...)                                                              \
    enum class Name : Underlying { __VA_ARGS__ };                                                   \
    [[maybe_unused]] constexpr ::std::string_view csyrenEnumSource(Name) noexcept { return #__VA_ARGS__; } \
    [[maybe_unused]] constexpr ::std::string_view csyrenEnumTypeName(Name) noexcept { return #Name; }

#define CS_ENUM_CONCAT_IMPL(a, b) a##b
#define CS_ENUM_CONCAT(a, b) CS_ENUM_CONCAT_IMPL(a, b)
// Use in a .cpp at namespace scope where `Type` is visible.
#define CS_ENUM_ALIAS(Type, OldName, Value)                                                         \
    static ::csyren::core::enums::AliasRegistrar<Type> CS_ENUM_CONCAT(_csEnumAlias_, __LINE__){ OldName, Type::Value };
