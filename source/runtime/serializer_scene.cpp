#include "serializer.h"
#include "core/scene.h"
#include "core/meta.h"
#include "core/scene_serialization_context.h"
#include "core/unknown_components.h"
#include "cstdmf/log.h"
#include <algorithm>
#include <mutex>
#include <optional>
#include <unordered_set>

namespace csyren
{
    namespace
    {
        template<class Read> bool readField(core::ArchiveReader& reader, core::reflection::LiteralID field, bool required, Read read, bool* exists = nullptr)
        {
            bool present{};
            if (!reader.beginField(field.id, field.str, present)) return false;
            if (exists) *exists = present;
            if (!present) return !required;
            const bool ok = read();
            return reader.endField() && ok;
        }
        void addName(std::vector<std::string>& names, std::string_view name)
        {
            if (std::find(names.begin(), names.end(), name) == names.end()) names.emplace_back(name);
        }
        // One warning per type name for the program's lifetime: the editor reloads
        // the scene from snapshots on every edit.
        void reportUnknown(const std::vector<std::string>& names, bool kept)
        {
            static std::mutex mutex;
            static std::unordered_set<std::string> reported;
            std::string text;
            {
                std::lock_guard lock(mutex);
                for (const auto& name : names)
                    if (reported.insert((kept ? "kept:" : "dropped:") + name).second) text += (text.empty() ? "" : ", ") + name;
            }
            if (text.empty()) return;
            if (kept) log::warning("Scene: unknown components kept as data (saved back unchanged): {}", text);
            else log::warning("Scene: unknown components skipped: {}", text);
        }
        // A component of an unknown type: kept in the entity's UnknownComponents.
        // Dropped (listed in `dropped`) when the format cannot capture values or
        // UnknownComponents is not registered.
        void keepUnknown(core::ArchiveReader& reader, core::EntityBatch& batch, core::Entity::ID entity, uint64_t id,
            const std::string& name, std::vector<std::string>& kept, std::vector<std::string>& dropped)
        {
            const auto label = name.empty() ? "#" + std::to_string(id) : name;
            core::UnknownComponents::Entry entry{ id, name, {} };
            if (name.empty() || !reader.capture(entry.value)) { addName(dropped, label); return; }
            auto* unknown = batch.getComponent<core::UnknownComponents>(entity);
            if (!unknown) unknown = batch.addComponent<core::UnknownComponents>(entity);
            if (!unknown) { addName(dropped, label); return; }
            unknown->set(std::move(entry));
            addName(kept, label);
        }
    }
    bool Serializer::saveScene(core::ArchiveWriter& writer, core::SerializationContext& context, core::Scene& scene)
    {
        context.reset();
        const auto unknownFamily = core::reflection::ComponentFamily::getID<core::UnknownComponents>();
        auto& references = context.emplaceService<core::SceneEntityReferences>();
        references.scene = &scene;
        for (const auto& entity : scene.entities())
            if (entity.id != core::EntityManager::ROOT_PARENT) references.saved.emplace(entity.id, references.saved.size());
        if (!writer.object() || !writer.beginField("$version"_hs.id, "$version") || !writer.unsignedInteger(2) || !writer.endField()) return false;
        if (!writer.beginField("entities"_hs.id, "entities") || !writer.sequence(references.saved.size())) return false;
        size_t index = 0;
        for (const auto& entity : scene.entities()) {
            if (entity.id == core::EntityManager::ROOT_PARENT) continue;
            if (!writer.beginElement(index++) || !writer.object()) return false;
            if (!writer.beginField("id"_hs.id, "id") || !writer.unsignedInteger(references.saved.at(entity.id)) || !writer.endField()) return false;
            if (entity.parent != core::EntityManager::ROOT_PARENT) {
                const auto parent = references.saved.find(entity.parent);
                if (parent == references.saved.end() || !writer.beginField("parent"_hs.id, "parent") || !writer.unsignedInteger(parent->second) || !writer.endField()) return false;
            }
            if (!writer.beginField("name"_hs.id, "name") || !writer.string(entity.name) || !writer.endField()) return false;
            if (!writer.beginField("components"_hs.id, "components") || !writer.object()) return false;
            const core::UnknownComponents* unknown = nullptr;
            std::vector<std::string_view> written;
            for (auto family : entity.componentView()) {
                if (family == unknownFamily) { unknown = static_cast<const core::UnknownComponents*>(scene.getComponentRaw(entity.id, family)); continue; }
                const auto* info = core::reflection::ComponentRegistry::get(family);
                if (!info || !info->metaType || info->metaType->id.id == 0) continue;
                if (!writer.beginField(info->metaType->id.id, info->name)) return false;
                const bool ok = core::reflection::save(writer, context, *info->metaType, info->getRaw(&scene, entity.id));
                if (!writer.endField() || !ok) return false;
                written.push_back(info->name);
            }
            // Kept unknown components go back as they were read (unless the type is known now).
            if (unknown)
                for (const auto& entry : unknown->entries) {
                    if (std::find(written.begin(), written.end(), entry.name) != written.end()) continue;
                    if (!writer.beginField(entry.id, entry.name) || !core::write(writer, entry.value) || !writer.endField()) return false;
                }
            if (!writer.endObject() || !writer.endField() || !writer.endObject() || !writer.endElement()) return false;
        }
        return writer.endSequence() && writer.endField() && context.saveSections(writer) && writer.endObject();
    }
    bool Serializer::loadScene(core::ArchiveReader& reader, core::SerializationContext& context, core::Scene& scene)
    {
        context.reset();
        auto& references = context.emplaceService<core::SceneEntityReferences>();
        uint32_t documentVersion{};
        struct ReferenceRollback {
            core::SceneEntityReferences& references;
            bool committed = false;
            ~ReferenceRollback() { if (!committed) references.loaded.clear(); }
        } referenceRollback{ references };
        if (!reader.object(documentVersion) || documentVersion > 2 || !context.loadSections(reader)) return false;
        bool present{};
        if (!reader.beginField("entities"_hs.id, "entities", present)) return false;
        if (!present) return reader.endObject() && context.finishLoad();
        size_t count{};
        if (!reader.sequence(count)) return false;
        std::vector<std::string> unknownNames, droppedNames;
        struct Record { core::Entity::ID entity; std::optional<uint64_t> parent; bool skip; };
        std::vector<Record> records; records.reserve(count);
        auto imported = scene.stageEntities(count);
        auto importScope = context.scopedService(imported);
        // Allocate every entity before reading any component, allowing forward references.
        for (size_t i = 0; i < count; ++i) {
            uint32_t version{};
            if (!reader.beginElement(i) || !reader.object(version) || version > 1) return false;
            std::string name; uint64_t localID = i, parent{}; bool hasID{}, hasParent{};
            if (!readField(reader, "name"_hs, true, [&] { return reader.string(name); }) ||
                !readField(reader, "id"_hs, documentVersion >= 2, [&] { return reader.unsignedInteger(localID); }, &hasID) ||
                !readField(reader, "parent"_hs, false, [&] { return reader.unsignedInteger(parent); }, &hasParent)) return false;
            // Legacy documents wrote their built-in root as runtime ID zero.
            const bool skip = documentVersion < 2 && hasID && localID == core::EntityManager::ROOT_PARENT;
            if (!skip) {
                const auto entity = imported.createEntity(name);
                if (!references.loaded.emplace(localID, entity).second) return false;
                records.push_back({ entity, hasParent ? std::optional<uint64_t>(parent) : std::nullopt, false });
            } else records.push_back({ core::EntityManager::ROOT_PARENT, {}, true });
            if (!reader.endObject() || !reader.endElement()) return false;
        }
        if (!reader.endSequence() || !reader.endField()) return false;
        for (const auto& record : records) {
            if (record.skip || !record.parent) continue;
            if (documentVersion < 2 && *record.parent == core::EntityManager::ROOT_PARENT) continue;
            const auto parent = references.loaded.find(*record.parent);
            if (parent == references.loaded.end() || !imported.setParent(record.entity, parent->second)) return false;
        }
        if (!imported.validateHierarchy()) return false;
        if (!reader.beginField("entities"_hs.id, "entities", present) || !present || !reader.sequence(count)) return false;
        if (count != records.size()) return false;
        for (size_t i = 0; i < count; ++i) {
            uint32_t version{};
            if (!reader.beginElement(i) || !reader.object(version)) return false;
            if (!records[i].skip) {
                if (!reader.beginField("components"_hs.id, "components", present)) return false;
                if (present) {
                    if (!reader.object(version) || version > 1 || !reader.beginFields()) return false;
                    uint64_t typeID{}; std::string name;
                    for (;;) {
                        bool hasComponent{};
                        if (!reader.nextField(typeID, name, hasComponent)) return false;
                        if (!hasComponent) break;
                        const core::reflection::ComponentMeta* info = nullptr;
                        if (!name.empty()) info = core::reflection::ComponentRegistry::get(name);
                        else for (const auto& [family, candidate] : core::reflection::ComponentRegistry::getAll())
                            if (candidate.metaType && candidate.metaType->id.id == typeID) { info = &candidate; break; }
                        bool ok = true;
                        if (info && info->metaType && info->metaType->id.id != 0)
                            ok = core::reflection::load(reader, context, *info->metaType, imported.addComponent(records[i].entity, info->family));
                        else keepUnknown(reader, imported, records[i].entity, typeID, name, unknownNames, droppedNames);
                        if (!reader.endField() || !ok) return false;
                    }
                    if (!reader.endFields() || !reader.endObject() || !reader.endField()) return false;
                }
            }
            if (!reader.endObject() || !reader.endElement()) return false;
        }
        if (!reader.endSequence() || !reader.endField() || !reader.endObject() || !context.finishLoad()) return false;
        reportUnknown(unknownNames, true);
        reportUnknown(droppedNames, false);
        if (!scene.append(std::move(imported))) return false;
        referenceRollback.committed = true;
        return true;
    }
}
