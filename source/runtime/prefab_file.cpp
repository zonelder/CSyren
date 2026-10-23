#include "prefab_file.h"
#include "core/scene.h"
#include "core/meta_json.h"
#include "core/project_paths.h"
#include "core/unknown_components.h"
#include "cstdmf/log.h"
#include <algorithm>
#include <atomic>
#include <filesystem>
#include <fstream>
#include <optional>
#include <unordered_map>
#ifdef _WIN32
#include "cstdmf/window_clean.h"
#else
#include <unistd.h>
#endif

namespace csyren
{
    namespace
    {
        // PrefabRef fields of a component value, through nested reflected structs.
        void collectPrefabReferences(const core::reflection::MetaType& type, const void* value,
            std::vector<std::string>& out, int depth = 0)
        {
            if (depth > 8) return;
            const auto* prefabType = core::reflection::resolve<core::PrefabRef>();
            for (const auto& field : type.data)
            {
                if (!field.type || !field.getConst) continue;
                const void* fieldValue = field.getConst(value);
                if (field.type == prefabType)
                {
                    const auto& reference = *static_cast<const core::PrefabRef*>(fieldValue);
                    if (reference.empty()) continue;
                    auto path = core::normalizePrefabPath(reference.path);
                    if (std::find(out.begin(), out.end(), path) == out.end()) out.push_back(std::move(path));
                }
                else if (!field.type->data.empty() && !field.type->enumInfo)
                    collectPrefabReferences(*field.type, fieldValue, out, depth + 1);
            }
        }
    }

    std::vector<core::Entity::ID> BranchArchive::collect(core::Scene& scene, core::Entity::ID root)
    {
        std::vector<core::Entity::ID> branch{ root };
        for (size_t i = 0; i < branch.size(); ++i)
        {
            const auto* entity = scene.entities().try_get(branch[i]);
            if (!entity || branch[i] == core::EntityManager::ROOT_PARENT) return {};
            branch.insert(branch.end(), entity->children.begin(), entity->children.end());
        }
        return branch;
    }

    bool BranchArchive::save(core::Scene& scene, const std::vector<core::Entity::ID>& branch,
        core::SerializationContext& context, std::string& error)
    {
        entities.clear();
        sections = nlohmann::json::object();
        auto* references = context.tryGet<core::SceneEntityReferences>();
        if (!references) { error = "no entity references in the serialization context"; return false; }
        std::unordered_map<core::Entity::ID, size_t> index;
        for (size_t i = 0; i < branch.size(); ++i) index.emplace(branch[i], i);
        for (size_t i = 0; i < branch.size(); ++i)
        {
            const auto* source = scene.entities().try_get(branch[i]);
            if (!source) { error = "an entity of the branch does not exist"; return false; }
            Entity entity;
            entity.name = source->name;
            const auto saved = references->saved.find(branch[i]);
            if (saved == references->saved.end()) { error = "entity '" + source->name + "' has no reference id"; return false; }
            entity.id = saved->second;
            if (i > 0)
            {
                const auto parent = index.find(source->parent);
                if (parent == index.end()) { error = "the branch is not a tree"; return false; }
                entity.parent = parent->second;
            }
            for (const auto family : source->componentView())
            {
                if (family == core::reflection::ComponentFamily::getID<core::UnknownComponents>())
                {
                    const auto* unknown = static_cast<const core::UnknownComponents*>(scene.getComponentRaw(branch[i], family));
                    if (unknown) for (const auto& kept : unknown->entries)
                    {
                        UnknownComponent component{ kept.name, {} };
                        core::JsonArchiveWriter writer(component.value);
                        if (!core::write(writer, kept.value)) { error = "unknown component '" + kept.name + "' cannot be saved"; return false; }
                        entity.unknown.push_back(std::move(component));
                    }
                    continue;
                }
                const auto* info = core::reflection::ComponentRegistry::get(family);
                // Components without reflection are not saved (as in scene files).
                if (!info || !info->metaType || info->metaType->id.id == 0) continue;
                Component component{ info, {} };
                core::JsonArchiveWriter writer(component.value);
                if (!core::reflection::save(writer, context, *info->metaType, info->getRaw(&scene, branch[i])))
                {
                    error = "component '" + std::string(info->name) + "' of '" + source->name +
                        "' cannot be saved (does it reference an entity outside the branch?)";
                    return false;
                }
                entity.components.push_back(std::move(component));
            }
            entities.push_back(std::move(entity));
        }
        core::JsonArchiveWriter writer(sections);
        if (!writer.object() || !context.saveSections(writer) || !writer.endObject())
        {
            error = "the resource table cannot be saved";
            return false;
        }
        return true;
    }

    bool BranchArchive::loadSections(core::SerializationContext& context, std::string& error) const
    {
        core::JsonArchiveReader reader(sections);
        uint32_t version{};
        if (reader.object(version) && context.loadSections(reader) && reader.endObject()) return true;
        error = "invalid resource table";
        return false;
    }

    std::vector<core::Entity::ID> BranchArchive::create(core::EntityBatch& batch, core::SceneEntityReferences& references) const
    {
        std::vector<core::Entity::ID> ids;
        ids.reserve(entities.size());
        for (const auto& entity : entities)
        {
            const auto id = batch.createEntity(entity.name);
            references.loaded[entity.id] = id;
            ids.push_back(id);
        }
        return ids;
    }

    bool BranchArchive::load(core::EntityBatch& batch, const std::vector<core::Entity::ID>& ids,
        core::SerializationContext& context, std::string& error) const
    {
        if (ids.size() != entities.size()) { error = "entity count mismatch"; return false; }
        for (size_t i = 0; i < entities.size(); ++i)
            if (entities[i].parent != npos && !batch.setParent(ids[i], ids[entities[i].parent]))
            { error = "invalid parent of '" + entities[i].name + "'"; return false; }
        for (size_t i = 0; i < entities.size(); ++i)
            for (const auto& component : entities[i].components)
            {
                void* target = batch.addComponent(ids[i], component.meta->family);
                core::JsonArchiveReader reader(component.value);
                if (!target || !core::reflection::load(reader, context, *component.meta->metaType, target))
                {
                    error = "component '" + std::string(component.meta->name) + "' of '" + entities[i].name + "' cannot be loaded";
                    return false;
                }
            }
        for (size_t i = 0; i < entities.size(); ++i)
        {
            if (entities[i].unknown.empty()) continue;
            auto* unknown = batch.addComponent<core::UnknownComponents>(ids[i]);
            if (!unknown) continue; // not registered: the values are dropped
            for (const auto& component : entities[i].unknown)
            {
                core::UnknownComponents::Entry entry{ cstdmf::fnv1a_32(component.name.data(), component.name.size()).id, component.name, {} };
                core::JsonArchiveReader reader(component.value);
                reader.capture(entry.value);
                unknown->set(std::move(entry));
            }
        }
        return true;
    }

    nlohmann::json BranchArchive::document() const
    {
        nlohmann::json document = sections.is_object() ? sections : nlohmann::json::object();
        document["$version"] = 2;
        auto list = nlohmann::json::array();
        for (const auto& entity : entities)
        {
            nlohmann::json item = nlohmann::json::object();
            item["id"] = entity.id;
            if (entity.parent != npos) item["parent"] = entities[entity.parent].id;
            item["name"] = entity.name;
            auto components = nlohmann::json::object();
            for (const auto& component : entity.components) components[std::string(component.meta->name)] = component.value;
            for (const auto& component : entity.unknown)
                if (!components.contains(component.name)) components[component.name] = component.value;
            item["components"] = std::move(components);
            list.push_back(std::move(item));
        }
        document["entities"] = std::move(list);
        return document;
    }

    bool BranchArchive::fromDocument(const nlohmann::json& document, std::string& error, std::vector<std::string>* unknownNames)
    {
        entities.clear();
        sections = nlohmann::json::object();
        if (!document.is_object()) { error = "not a JSON object"; return false; }
        if (document.value("$version", 0u) > 2u) { error = "unsupported document version"; return false; }
        const auto list = document.find("entities");
        if (list == document.end() || !list->is_array() || list->empty()) { error = "no entities"; return false; }
        struct Raw { uint64_t id; std::optional<uint64_t> parent; std::string name; const nlohmann::json* components; };
        std::vector<Raw> raw;
        std::unordered_map<uint64_t, size_t> byId;
        for (size_t i = 0; i < list->size(); ++i)
        {
            const auto& item = (*list)[i];
            if (!item.is_object()) { error = "an entity is not an object"; return false; }
            Raw entry{ item.value("id", uint64_t(i)), std::nullopt, item.value("name", std::string()), nullptr };
            if (const auto parent = item.find("parent"); parent != item.end())
            {
                if (!parent->is_number_unsigned()) { error = "invalid parent of '" + entry.name + "'"; return false; }
                entry.parent = parent->get<uint64_t>();
            }
            if (const auto components = item.find("components"); components != item.end())
            {
                if (!components->is_object()) { error = "invalid components of '" + entry.name + "'"; return false; }
                entry.components = &*components;
            }
            if (!byId.emplace(entry.id, raw.size()).second) { error = "duplicate entity id " + std::to_string(entry.id); return false; }
            raw.push_back(std::move(entry));
        }
        size_t root = npos, roots = 0;
        for (size_t i = 0; i < raw.size(); ++i) if (!raw[i].parent) { root = i; ++roots; }
        if (roots != 1) { error = "a prefab has exactly one root entity (found " + std::to_string(roots) + ")"; return false; }
        // Breadth-first from the root: parents come before their children.
        std::unordered_map<uint64_t, std::vector<size_t>> children;
        for (size_t i = 0; i < raw.size(); ++i) if (raw[i].parent) children[*raw[i].parent].push_back(i);
        std::vector<size_t> order{ root };
        std::vector<size_t> position(raw.size(), npos);
        position[root] = 0;
        for (size_t i = 0; i < order.size(); ++i)
            if (auto it = children.find(raw[order[i]].id); it != children.end())
                for (const auto child : it->second) { position[child] = order.size(); order.push_back(child); }
        if (order.size() != raw.size()) { error = "some entities are not under the root (unknown parent or a cycle)"; return false; }
        for (const auto index : order)
        {
            const auto& source = raw[index];
            Entity entity;
            entity.name = source.name;
            entity.id = source.id;
            if (source.parent) entity.parent = position[byId.at(*source.parent)];
            if (source.components)
                for (const auto& [name, value] : source.components->items())
                {
                    const auto* info = core::reflection::ComponentRegistry::get(name);
                    if (!info || !info->metaType || info->metaType->id.id == 0)
                    {
                        if (unknownNames) unknownNames->push_back(name);
                        entity.unknown.push_back({ name, value });
                        continue;
                    }
                    entity.components.push_back({ info, value });
                }
            entities.push_back(std::move(entity));
        }
        for (const auto& [key, value] : document.items())
            if (key != "$version" && key != "entities") sections[key] = value;
        return true;
    }

    PrefabFile::PrefabFile(std::string path, BranchArchive archive, ConfigureArchive configure)
        : _path(std::move(path)), _archive(std::move(archive)), _configure(std::move(configure))
    {
        if (_configure) _configure(_context);
        _context.emplaceService<core::SceneEntityReferences>();
    }

    bool PrefabFile::prepare(std::string& error)
    {
        std::lock_guard lock(_mutex);
        if (!_archive.loadSections(_context, error)) return false;
        // Read every component once into a scratch value: the prefab is validated
        // when it is loaded, not on its first spawn, its resources are looked up
        // now (cached in the context) instead of in the middle of a frame, and the
        // prefabs it refers to are found (the library loads them too).
        _references.clear();
        auto& references = *_context.tryGet<core::SceneEntityReferences>();
        references.loaded.clear();
        for (const auto& entity : _archive.entities) references.loaded[entity.id] = core::Entity::invalidID;
        for (const auto& entity : _archive.entities)
            for (const auto& component : entity.components)
            {
                if (!component.meta->createValue) continue; // not copyable: checked on the first copy
                const auto value = component.meta->createValue();
                core::JsonArchiveReader reader(component.value);
                if (!value || !core::reflection::load(reader, _context, *component.meta->metaType, value.get()))
                {
                    error = "component '" + std::string(component.meta->name) + "' of '" + entity.name + "' cannot be loaded";
                    references.loaded.clear();
                    return false;
                }
                collectPrefabReferences(*component.meta->metaType, value.get(), _references);
            }
        references.loaded.clear();
        return _context.finishLoad();
    }

    bool PrefabFile::tryInstantiate(core::Scene& scene, const core::SpawnPose& pose,
        const std::vector<core::PrefabEdit>& edits, core::Entity::ID& root, std::string& error) const
    {
        auto& references = *_context.tryGet<core::SceneEntityReferences>();
        references.loaded.clear();
        auto batch = scene.stageEntities(_archive.entities.size());
        std::vector<core::Entity::ID> ids;
        {
            auto scope = _context.scopedService(batch);
            ids = _archive.create(batch, references);
            if (!_archive.load(batch, ids, _context, error) || !_context.finishLoad()) return false;
        }
        root = core::finishInstance(scene, std::move(batch), ids.front(), pose, edits, *this);
        return true; // a failed append is logged by finishInstance and not retried
    }

    core::Entity::ID PrefabFile::instantiate(core::Scene& scene, const core::SpawnPose& pose,
        const std::vector<core::PrefabEdit>& edits) const
    {
        std::lock_guard lock(_mutex);
        core::Entity::ID root = core::Entity::invalidID;
        std::string error;
        if (tryInstantiate(scene, pose, edits, root, error)) return root;
        // Resolved resources are cached in the context; a resource may have been
        // reloaded since. Read the resource table again and retry once.
        std::string retryError;
        if (_archive.loadSections(_context, retryError) && tryInstantiate(scene, pose, edits, root, retryError)) return root;
        core::logOnce(_path + "#instantiate", "Prefab '" + _path + "' cannot be instantiated: " + error);
        return core::Entity::invalidID;
    }

    std::shared_ptr<const PrefabFile> loadPrefabFile(const std::string& path, const ConfigureArchive& configure, std::string& error)
    {
        try
        {
            const auto normalized = core::normalizePrefabPath(path);
            const auto file = core::projectAssetPath(core::pathFromUtf8(normalized));
            std::ifstream in(file, std::ios::binary);
            if (!in) { error = "file not found: " + core::pathToUtf8(file); return nullptr; }
            auto document = nlohmann::json::parse(in, nullptr, false);
            if (document.is_discarded()) { error = "invalid JSON in " + core::pathToUtf8(file); return nullptr; }
            BranchArchive archive;
            std::vector<std::string> unknown;
            if (!archive.fromDocument(document, error, &unknown)) return nullptr;
            if (!unknown.empty())
            {
                std::sort(unknown.begin(), unknown.end());
                unknown.erase(std::unique(unknown.begin(), unknown.end()), unknown.end());
                std::string names;
                for (const auto& name : unknown) names += (names.empty() ? "" : ", ") + name;
                log::warning("Prefab '{}': unknown components kept as data (copies carry them): {}", normalized, names);
            }
            auto prefab = std::make_shared<PrefabFile>(normalized, std::move(archive), configure);
            if (!prefab->prepare(error)) return nullptr;
            return prefab;
        }
        catch (const std::exception& exception)
        {
            error = exception.what();
            return nullptr;
        }
    }

    core::PrefabLibrary::Loader prefabFileLoader(ConfigureArchive configure)
    {
        return [configure = std::move(configure)](const std::string& path, std::string& error) -> core::PrefabHandle {
            return loadPrefabFile(path, configure, error);
        };
    }

    bool savePrefabFile(core::Scene& scene, core::Entity::ID root, const std::string& path,
        const ConfigureArchive& configure, std::string& error)
    {
        try
        {
            const auto branch = BranchArchive::collect(scene, root);
            if (branch.empty()) { error = "the entity does not exist"; return false; }
            core::SerializationContext context;
            if (configure) configure(context);
            auto& references = context.emplaceService<core::SceneEntityReferences>();
            for (size_t i = 0; i < branch.size(); ++i) references.saved.emplace(branch[i], i);
            BranchArchive archive;
            if (!archive.save(scene, branch, context, error)) return false;
            const auto text = archive.document().dump(4);
            const auto target = core::projectAssetPath(core::pathFromUtf8(core::normalizePrefabPath(path)));
            if (target.has_parent_path()) std::filesystem::create_directories(target.parent_path());
            // Written next to the target and renamed over it (as scene files are):
            // a failed save leaves neither a partial prefab nor a temporary file.
#ifdef _WIN32
            const auto process = static_cast<uint64_t>(GetCurrentProcessId());
#else
            const auto process = static_cast<uint64_t>(getpid());
#endif
            static std::atomic<uint64_t> nextTemporary{};
            auto temporary = target;
            temporary += ".tmp." + std::to_string(process) + "." + std::to_string(++nextTemporary);
            struct Cleanup {
                std::filesystem::path path;
                ~Cleanup() { std::error_code ignored; std::filesystem::remove(path, ignored); }
            } cleanup{ temporary };
            {
                std::ofstream file(temporary, std::ios::binary | std::ios::trunc);
                if (!file) { error = "cannot write " + core::pathToUtf8(temporary); return false; }
                file.write(text.data(), static_cast<std::streamsize>(text.size()));
                file.close();
                if (!file.good()) { error = "cannot write " + core::pathToUtf8(temporary); return false; }
            }
#ifdef _WIN32
            const bool replaced = MoveFileExW(temporary.c_str(), target.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0;
#else
            std::error_code renameError;
            std::filesystem::rename(temporary, target, renameError);
            const bool replaced = !renameError;
#endif
            if (!replaced) { error = "cannot replace " + core::pathToUtf8(target); return false; }
            return true;
        }
        catch (const std::exception& exception)
        {
            error = exception.what();
            return false;
        }
    }
}
