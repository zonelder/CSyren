#pragma once
#include <string_view>
#include <unordered_map>

#include "core/entity.h"
#include "cstdmf/log.h"
#include "meta.h"

#include "third_party/json/json.hpp"

using json = nlohmann::json;

namespace csyren::core
{
    class Scene;
}


namespace csyren::core::reflection
{

    /**
     * @struct ComponentMeta
     * @brief Contain reflection data of components that allow serializer work with component in polymorphic way.
     */
    struct ComponentMeta
    {
        using AddFn = void* (*)(Scene*, Entity::ID);
        using HasFn = bool  (*)(Scene*, Entity::ID);
        using GetRawFn = void* (*)(Scene*, Entity::ID);
        using RemoveFn = void  (*)(Scene*, Entity::ID);
        using DestroyFn = bool(*)(Scene*, Entity::ID);
        using SerializeFn = void  (*)(const void*, json&);
        using DeserializeFn = void  (*)(void*, const json&);

        AddFn           add = nullptr;
        HasFn           has = nullptr;
        GetRawFn        getRaw = nullptr;
        RemoveFn        remove = nullptr;
        DestroyFn       destroy = nullptr;
        SerializeFn     serialize = nullptr;
        DeserializeFn   deserialize = nullptr;

        std::string_view name; // Полезно для дебага и логов
    };

    template<class T> class ComponentRegistrar;
    /**
     * @class ComponentRegistry
     * @brief global, stateless registry of operations with components.
     *        do not save scene data.
     */
    class ComponentRegistry
    {
        template<typename T>
        friend class ComponentRegistrar;

    public:
        using Registries = std::unordered_map<ComponentFamily::typeID, ComponentMeta>;

        [[nodiscard]] static const ComponentMeta* get(ComponentFamily::typeID family)
        {
            const auto& registry = getRegistry();
            auto it = registry.find(family);
            return (it != registry.end()) ? &it->second : nullptr;
        }

        [[nodiscard]] static const ComponentMeta* get(std::string_view name)
        {
            const auto& registry = getRegistry();
            for (const auto& [_, meta] : registry)
            {
                if (meta.name == name)
                {
                    return &meta;
                }
            }
            return nullptr;
        }

        [[nodiscard]] static const Registries& getAll()
        {
            return getRegistry();
        }

    private:
        template<typename T>
        static void registerComponentImpl(std::string_view name);

        [[nodiscard]] static Registries& getRegistry()
        {
            static Registries s_componentInfo;
            return s_componentInfo;
        }
    };


    /**
     * @class ComponentRegistrar
     * @brief Хелпер для извлечения логики сериализации конкретного типа T.
     */
    template<class T>
    class ComponentRegistrar
    {
    public:
        explicit ComponentRegistrar(std::string_view name)
        {
            ComponentRegistry::registerComponentImpl<T>(name);
        }

        static void serialize_impl(const void* comp, json& j)
        {
            if (comp) {
                static_cast<const T*>(comp)->serialize(j);
            }
        }

        static void deserialize_impl(void* comp, const json& j)
        {
            if (comp) 
            {
                static_cast<T*>(comp)->deserialize(j);
            }
        }
    };
}

#define REGISTER_COMPONENT(Type) \
        REFLECT(Type)            \
        static const ::csyren::core::reflection::ComponentRegistrar<Type> _##Type##_registrar( #Type );
