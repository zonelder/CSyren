

namespace csyren::core::reflection
{
    template<class T>
    inline void ComponentRegistry::registerComponentImpl(std::string_view name)
    {
        auto& registry = getRegistry();

        auto family = ComponentFamily::getID<T>();
        CS_DEBUG_ASSERT_MSG(registry.find(family) == registry.end(), "ComponentRegistry: This component already registered\n");

        ComponentMeta meta;
        meta.name = name;

        meta.add = [](Scene* scene, Entity::ID id) -> void*
            {
                return scene->template addComponent<T>(id).get();
            };

        meta.has = [](Scene* scene, Entity::ID id) -> bool
            {

                return scene->template hasComponent<T>(id);
            };

        meta.getRaw = [](Scene* scene, Entity::ID id) -> void*
            {
                auto pool = scene->getPool<T>();
                return pool ? pool->try_get(id) : nullptr;
            };

        meta.remove = [](Scene* scene, Entity::ID id) -> void
            {
                scene->template removeComponent<T>(id);
            };
        meta.destroy = [](Scene* scene, Entity::ID id) -> bool
            {
               return scene->template destroy<T>(id);
            };

        if constexpr (requires(T & t, json & j2) { t.serialize(j2); })
        {
            meta.serialize = &ComponentRegistrar<T>::serialize_impl;
        }
        if constexpr (requires(T & t, json & j2) { t.deserialize(j2); })
        {
            meta.deserialize = &ComponentRegistrar<T>::deserialize_impl;
        }
        registry[family] = std::move(meta);
        log::debug("ComponentRegistry: '{}' registered successfully.family = {}", name, family);
    }

};