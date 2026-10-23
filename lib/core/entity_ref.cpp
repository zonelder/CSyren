#include "pch.h"
#include "entity_ref.h"
#include "meta.h"
#include "scene.h"
#include "scene_serialization_context.h"

namespace csyren::core
{
    void EntityRef::describe()
    {
        reflection::MetaFactory<EntityRef>{}.primitiveType("EntityRef"_hs).archive({
            [](ArchiveWriter& writer, SerializationContext& context, const reflection::MetaType&, const void* value) -> bool {
                const auto id = static_cast<const EntityRef*>(value)->id;
                if (id != Entity::invalidID) {
                    const auto* references = context.tryGet<SceneEntityReferences>();
                    if (!references) return false;
                    if (const auto it = references->saved.find(id); it != references->saved.end())
                        return writer.unsignedInteger(it->second);
                    // A live entity that is not in the document cannot be referenced from it.
                    if (!references->scene || references->scene->entities().contains(id)) return false;
                }
                return writer.signedInteger(-1); // empty, or the entity was destroyed
            },
            [](ArchiveReader& reader, SerializationContext& context, const reflection::MetaType&, void* value) -> bool {
                int64_t number{};
                if (!reader.signedInteger(number)) return false;
                auto& reference = *static_cast<EntityRef*>(value);
                if (number < 0) { reference = {}; return true; }
                const auto* references = context.tryGet<SceneEntityReferences>();
                if (!references) return false;
                const auto it = references->loaded.find(static_cast<uint64_t>(number));
                if (it == references->loaded.end()) return false;
                reference.id = it->second;
                return true;
            } });
    }
}
