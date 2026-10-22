#pragma once
// RTTI-free type identity and type names. The engine builds with /GR- and must
// not use typeid / std::type_info / std::type_index (tools/csyren_registry.py
// rejects them); use TypeKey::of<T>() and typeName<T>() instead.
//
//   TypeKey::of<T>()   a unique key per type (const/volatile/reference stripped),
//                      usable as a hash map key; key.name is typeName<T>().
//   typeName<T>()      the compiler's spelling of T, computed at compile time
//                      (e.g. "csyren::core::Transform"); for logs, errors and UI.
//
// Keys identify types within one running image (static linking); they are not
// stable across builds and must not be serialized.
#include <cstddef>
#include <functional>
#include <string_view>
#include <type_traits>

namespace csyren::core
{
    namespace details
    {
        template<class T> constexpr std::string_view rawTypeName() noexcept
        {
#if defined(_MSC_VER) && !defined(__clang__)
            return __FUNCSIG__;
#else
            return __PRETTY_FUNCTION__;
#endif
        }
        // The text around the type in rawTypeName<T>() is the same for every T;
        // measure it once with a probe type that occurs nowhere else in it.
        inline constexpr std::string_view typeNameProbe = rawTypeName<double>();
        inline constexpr std::size_t typeNamePrefix = typeNameProbe.find("double");
        inline constexpr std::size_t typeNameSuffix = typeNameProbe.size() - typeNamePrefix - std::string_view("double").size();
        static_assert(typeNamePrefix != std::string_view::npos, "typeName<T>(): unsupported compiler");

        constexpr std::string_view withoutKeyword(std::string_view name) noexcept
        {
            // MSVC spells class types with their keyword ("struct csyren::core::Transform").
            for (std::string_view keyword : { "struct ", "class ", "enum ", "union " })
                if (name.substr(0, keyword.size()) == keyword) return name.substr(keyword.size());
            return name;
        }

        // One writable byte per type: its address is the key. Writable data is never
        // merged by identical-COMDAT folding (/OPT:ICF), so keys of different types differ.
        template<class T> struct TypeTag { static inline char id = 0; };
    }

    template<class T> constexpr std::string_view typeName() noexcept
    {
        constexpr std::string_view raw = details::rawTypeName<T>();
        std::string_view name = raw.substr(details::typeNamePrefix,
            raw.size() - details::typeNamePrefix - details::typeNameSuffix);
        // MSVC separates closing angle brackets: "...<int> > >(void)" leaves a
        // trailing space after a template type.
        while (!name.empty() && name.back() == ' ') name.remove_suffix(1);
        return details::withoutKeyword(name);
    }

    struct TypeKey
    {
        const void* id = nullptr;
        std::string_view name;

        template<class T> static constexpr TypeKey of() noexcept
        {
            using U = std::remove_cvref_t<T>;
            return { &details::TypeTag<U>::id, typeName<U>() };
        }
        explicit constexpr operator bool() const noexcept { return id != nullptr; }
        friend constexpr bool operator==(const TypeKey& a, const TypeKey& b) noexcept { return a.id == b.id; }
    };
}

template<> struct std::hash<csyren::core::TypeKey>
{
    std::size_t operator()(const csyren::core::TypeKey& key) const noexcept { return std::hash<const void*>{}(key.id); }
};
