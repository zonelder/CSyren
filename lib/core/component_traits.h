#pragma once

namespace csyren::core::reflection
{
	template<typename T>
    struct ComponentTraits
    {
        static constexpr std::string_view name = "UnknownComponent";
    };
}

#define CS_REGISTER_COMPONENT(Type, DisplayName) \
    namespace csyren::core::reflection { \
        template<> struct ComponentTraits<Type> { \
            static constexpr std::string_view name = #DisplayName; \
            inline static uint64_t typeID = ComponentFamily::getID<Type>(); }; \
    }

