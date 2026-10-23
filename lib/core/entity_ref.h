#pragma once
// core::EntityRef: a component field that refers to another entity.
//
// It holds an Entity::ID, so a reference to a destroyed entity finds nothing
// (IDs carry a generation). Unlike a plain Entity::ID field it is saved and
// follows the entities it refers to:
//   scene file   the number of the referenced entity in the document; -1 when
//                the reference is empty or its entity was destroyed;
//   load         points at the loaded entities;
//   Duplicate    references inside the copied branch point at the copies, the
//                ones leaving it keep pointing at the originals;
//   prefab       references inside the prefab point at the copy's entities; a
//                reference leaving the branch cannot be saved (an error names
//                the component).
// The serialization context needs SceneEntityReferences (scene_serialization_context.h).
#include "entity.h"
#include "registration.h"

namespace csyren::core
{
    struct EntityRef
    {
        Entity::ID id = Entity::invalidID;
        bool empty() const noexcept { return id == Entity::invalidID; }
        bool operator==(const EntityRef&) const = default;
        static void describe(); // entity_ref.cpp
    };
    CS_REFLECT(EntityRef);
}
