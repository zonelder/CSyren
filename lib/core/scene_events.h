#ifndef __CSYREN_SCENE_EVENTS__
#define __CSYREN_SCENE_EVENTS__

#include "event_bus.h"
#include "entity.h"
#include "component_base.h"

namespace csyren::core {
    class Scene;

    struct EntityCreatedEvent 
    {
        Scene& scene;
        Entity::ID id;
    };

    struct EntityDestroyedEvent
    {
        Scene& scene;
        Entity::ID id;
    };

    struct ComponentAddedEvent 
    {
        Scene& scene;
        Entity::ID entity;
        size_t family;
        Component::ID component;
    };

    struct ComponentRemovedEvent 
    {
        Scene& scene;
        Entity::ID entity;
        size_t family;
        Component::ID component;
    };

    inline const events::EventID kEntityCreatedID{ "EntityCreated" };
    inline const events::EventID kEntityDestroyedID{ "EntityDestroyed" };
    inline const events::EventID kComponentAddedID{ "ComponentAdded" };
    inline const events::EventID kComponentRemovedID{ "ComponentRemoved" };
}

#endif // CSYREN_SCENE_EVENTS_H
