#pragma once
#include "entity.h"
#include <unordered_map>

namespace csyren::core
{
    class Scene;

    // Document-local identity, independent of runtime IDs and destination contents.
    // Component reference operations may use this mapping during serialization.
    struct SceneEntityReferences
    {
        std::unordered_map<Entity::ID, uint64_t> saved;
        std::unordered_map<uint64_t, Entity::ID> loaded;
        // The scene being saved: tells a reference to a destroyed entity (saved as
        // none) from one to an entity outside the document (an error). Optional.
        const Scene* scene = nullptr;
    };
}
