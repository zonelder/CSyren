#pragma once
#include "entity.h"
#include <unordered_map>

namespace csyren::core
{
    // Document-local identity, independent of runtime IDs and destination contents.
    // Component reference operations may use this mapping during serialization.
    struct SceneEntityReferences
    {
        std::unordered_map<Entity::ID, uint64_t> saved;
        std::unordered_map<uint64_t, Entity::ID> loaded;
    };
}
