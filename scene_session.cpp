#include "scene_session.h"
#include "core/transform.h"
#include "core/meta_json.h"
#include "core/scene_serialization_context.h"
#include <stdexcept>

namespace csyren::editor
{
    SceneSession::SceneSession(SceneStorage& storage)
        : _storage(storage), _editor(std::make_unique<core::SceneInstance>()) {}

    void SceneSession::rebind()
    { if (_bind) _bind(editorContext(), activeContext(), playing()); }

    void SceneSession::settle()
    {
        _editor->settle();
        if (_runtime) _runtime->settle();
    }

    EntityKey SceneSession::keyFor(core::Entity::ID id)
    {
        if (!editorScene().entities().contains(id) || id == core::EntityManager::ROOT_PARENT) return 0;
        for (const auto& [key, entity] : _entities) if (entity == id) return key;
        const auto key = ++_nextKey;
        _entities.emplace(key, id); return key;
    }

    core::Entity::ID SceneSession::resolve(EntityKey key) const
    {
        auto it = _entities.find(key);
        return it == _entities.end() ? core::Entity::invalidID : it->second;
    }

    void SceneSession::finishEdit()
    {
        if (!_edit) return;
        if (_edit->changed()) _history.record(std::move(_edit));
        else _edit.reset();
    }

    bool SceneSession::cancelEdit()
    {
        if (!_edit) return true;
        auto context = editContext();
        if (!_edit->undo(context)) return false;
        _edit.reset(); return true;
    }

    EntityMap SceneSession::mapKeys(core::Scene& scene, const Keys& keys)
    {
        EntityMap result; size_t index{};
        for (const auto& entity : scene.entities())
        {
            if (entity.id == core::EntityManager::ROOT_PARENT) continue;
            const auto key = index < keys.size() ? keys[index] : ++_nextKey;
            result.emplace(key, entity.id); ++index;
        }
        if (!keys.empty() && index < keys.size()) throw std::runtime_error("Scene snapshot lost entities");
        return result;
    }

    bool SceneSession::replaceEditor(std::unique_ptr<core::SceneInstance>& candidate, EntityMap keys)
    {
        candidate->settle(); // loading events finish before consumers scan the scene
        _editor.swap(candidate);
        try { rebind(); }
        catch (...)
        {
            _editor.swap(candidate); rebind(); throw;
        }
        _entities.swap(keys);
        return true;
    }

    bool SceneSession::capture(Snapshot& snapshot)
    {
        snapshot.keys.clear();
        for (const auto& entity : editorScene().entities())
            if (entity.id != core::EntityManager::ROOT_PARENT) snapshot.keys.push_back(keyFor(entity.id));
        return _storage.snapshot(editorScene(), snapshot.archive);
    }

    bool SceneSession::restore(const Snapshot& snapshot)
    {
        auto candidate = std::make_unique<core::SceneInstance>();
        if (!_storage.restore(snapshot.archive, candidate->scene())) return false;
        auto keys = mapKeys(candidate->scene(), snapshot.keys);
        return replaceEditor(candidate, std::move(keys));
    }

    bool SceneSession::open(const std::string& path, bool discard)
    {
        if (path.empty()) { _error = "Choose a scene file first."; return false; }
        if (dirty() && !discard) { _error = "Save or discard changes before replacing the scene."; return false; }
        auto candidate = std::make_unique<core::SceneInstance>();
        if (!_storage.read(path, candidate->scene())) return false;
        auto keys = mapKeys(candidate->scene(), {});
        replaceEditor(candidate, std::move(keys));
        _path = path; ++_document; _history.reset();
        return true;
    }

    bool SceneSession::save(const std::string& path)
    {
        if (path.empty()) { _error = "Choose a path with Save As first."; return false; }
        if (!_storage.write(path, editorScene())) return false;
        _path = path; _history.markSaved(); return true;
    }

    bool SceneSession::import(const std::string& path)
    {
        auto before = std::make_shared<Snapshot>();
        auto after = std::make_shared<Snapshot>();
        if (!capture(*before)) return false;
        auto candidate = std::make_unique<core::SceneInstance>();
        if (!_storage.restore(before->archive, candidate->scene()) || !_storage.read(path, candidate->scene())) return false;
        if (!_storage.snapshot(candidate->scene(), after->archive)) return false;
        auto keys = mapKeys(candidate->scene(), before->keys);
        for (const auto& entity : candidate->scene().entities())
        {
            if (entity.id == core::EntityManager::ROOT_PARENT) continue;
            for (const auto& [key, id] : keys) if (entity.id == id) { after->keys.push_back(key); break; }
        }
        // Restore on execute too: preparation and commit obey the same contract.
        auto command = std::make_unique<FunctionCommand>("Import scene",
            [this, after](EditContext&) { return restore(*after); },
            [this, before](EditContext&) { return restore(*before); });
        auto context = editContext();
        return _history.execute(std::move(command), context);
    }

    bool SceneSession::create(const math::Vector3& position)
    {
        const auto key = ++_nextKey;
        auto command = std::make_unique<FunctionCommand>("Create entity",
            [this, key, position](EditContext& context)
            {
                auto batch = context.scene.stageEntities(1);
                auto entity = batch.createEntity("New object");
                if (!batch.addComponent<core::Transform>(entity)) return false;
                batch.getComponent<core::Transform>(entity)->position = position;
                context.entities.reserve(context.entities.size() + 1);
                auto [it, inserted] = context.entities.emplace(key, entity);
                if (!inserted) return false;
                try
                {
                    if (!context.scene.append(std::move(batch))) { context.entities.erase(it); return false; }
                }
                catch (...) { context.entities.erase(it); throw; }
                _createdKey = key;
                return true;
            },
            [key](EditContext& context)
            {
                const auto entity = context.resolve(key);
                if (!context.scene.entities().contains(entity)) return false;
                context.scene.destroyEntity(entity); context.scene.flush();
                context.entities.erase(key); return true;
            });
        auto context = editContext();
        return _history.execute(std::move(command), context);
    }

    bool SceneSession::changeComponent(EntityKey key, size_t family, bool add)
    {
        const auto* meta = core::reflection::ComponentRegistry::get(family);
        const auto id = resolve(key);
        if (!meta || !meta->metaType || !meta->createValue || !meta->addValue ||
            !meta->metaType->archive.save || !meta->metaType->archive.load ||
            id == core::EntityManager::ROOT_PARENT || !editorScene().entities().contains(id)) return false;
        if (meta->has(&editorScene(), id) == add) return false;
        auto archive = std::make_shared<nlohmann::json>();
        if (!add)
        {
            core::SerializationContext serialization;
            _storage.configureComponentArchive(serialization);
            auto& refs = serialization.emplaceService<core::SceneEntityReferences>();
            for (const auto& entity : editorScene().entities())
                if (entity.id != core::EntityManager::ROOT_PARENT) refs.saved.emplace(entity.id, keyFor(entity.id));
            core::JsonArchiveWriter writer(*archive);
            if (!writer.object() || !writer.beginField("value"_hs.id, "value") ||
                !core::reflection::save(writer, serialization, *meta->metaType, meta->getRaw(&editorScene(), id)) ||
                !writer.endField() || !serialization.saveSections(writer) || !writer.endObject()) return false;
        }
        const auto remove = [key, family](EditContext& context) {
            const auto* info = core::reflection::ComponentRegistry::get(family);
            const auto target = context.resolve(key);
            if (!context.scene.entities().contains(target) || !info->has(&context.scene, target)) return false;
            info->remove(&context.scene, target); context.scene.flush();
            return !info->has(&context.scene, target);
        };
        const auto insert = [this, key, family, archive, add](EditContext& context) {
            const auto* info = core::reflection::ComponentRegistry::get(family);
            const auto target = context.resolve(key);
            if (!context.scene.entities().contains(target) || info->has(&context.scene, target)) return false;
            if (add) return info->add(&context.scene, target) != nullptr;
            auto value = info->createValue();
            core::SerializationContext serialization;
            _storage.configureComponentArchive(serialization);
            auto& refs = serialization.emplaceService<core::SceneEntityReferences>();
            for (const auto& [entityKey, entity] : context.entities) refs.loaded.emplace(entityKey, entity);
            core::JsonArchiveReader reader(*archive);
            uint32_t version{}; bool present{};
            if (!reader.object(version) || !serialization.loadSections(reader) ||
                !reader.beginField("value"_hs.id, "value", present) || !present ||
                !core::reflection::load(reader, serialization, *info->metaType, value.get()) ||
                !reader.endField() || !reader.endObject() || !serialization.finishLoad()) return false;
            return info->addValue(&context.scene, target, value.get()) != nullptr;
        };
        auto command = std::make_unique<FunctionCommand>(std::string(add ? "Add " : "Remove ") + std::string(meta->name),
            add ? FunctionCommand::Action(insert) : FunctionCommand::Action(remove),
            add ? FunctionCommand::Action(remove) : FunctionCommand::Action(insert));
        auto context = editContext();
        return _history.execute(std::move(command), context);
    }

    bool SceneSession::changeBranch(EntityKey key, bool duplicate)
    {
        const auto source = resolve(key);
        if (!key || source == core::EntityManager::ROOT_PARENT || !editorScene().entities().contains(source)) return false;
        auto before = std::make_shared<Snapshot>();
        auto after = std::make_shared<Snapshot>();
        if (!capture(*before)) return false;
        auto candidate = std::make_unique<core::SceneInstance>();
        if (!_storage.restore(before->archive, candidate->scene())) return false;
        auto keys = mapKeys(candidate->scene(), before->keys);
        auto& scene = candidate->scene();
        const auto root = keys.at(key);
        EntityKey copyKey = 0;
        if (duplicate)
        {
            std::vector<core::Entity::ID> branch{ root };
            for (size_t i = 0; i < branch.size(); ++i)
            {
                const auto* entity = scene.entities().try_get(branch[i]);
                branch.insert(branch.end(), entity->children.begin(), entity->children.end());
            }
            core::SerializationContext saving;
            _storage.configureComponentArchive(saving);
            auto& savedRefs = saving.emplaceService<core::SceneEntityReferences>();
            for (const auto& [entityKey, id] : keys) savedRefs.saved.emplace(id, entityKey);
            struct Component { size_t family; nlohmann::json value; };
            std::vector<std::vector<Component>> components(branch.size());
            for (size_t i = 0; i < branch.size(); ++i)
                for (const auto family : scene.entities().try_get(branch[i])->componentView())
                {
                    const auto* info = core::reflection::ComponentRegistry::get(family);
                    if (!info || !info->metaType) return false;
                    Component component{ family, {} };
                    core::JsonArchiveWriter writer(component.value);
                    if (!core::reflection::save(writer, saving, *info->metaType, info->getRaw(&scene, branch[i]))) return false;
                    components[i].push_back(std::move(component));
                }
            nlohmann::json sections;
            core::JsonArchiveWriter sectionWriter(sections);
            if (!sectionWriter.object() || !saving.saveSections(sectionWriter) || !sectionWriter.endObject()) return false;
            core::SerializationContext loading;
            _storage.configureComponentArchive(loading);
            auto& loadedRefs = loading.emplaceService<core::SceneEntityReferences>();
            for (const auto& [entityKey, id] : keys) loadedRefs.loaded.emplace(entityKey, id);
            auto batch = scene.stageEntities(branch.size());
            auto scope = loading.scopedService(batch);
            std::unordered_map<core::Entity::ID, core::Entity::ID> copies;
            for (size_t i = 0; i < branch.size(); ++i)
            {
                const auto* entity = scene.entities().try_get(branch[i]);
                const auto copy = batch.createEntity(entity->name + (i == 0 ? " (Copy)" : ""));
                copies.emplace(branch[i], copy);
                loadedRefs.loaded[savedRefs.saved.at(branch[i])] = copy;
                const auto newKey = ++_nextKey;
                keys.emplace(newKey, copy);
                if (i == 0) copyKey = newKey;
            }
            core::JsonArchiveReader sectionReader(sections);
            uint32_t version{};
            if (!sectionReader.object(version) || !loading.loadSections(sectionReader) || !sectionReader.endObject()) return false;
            for (size_t i = 0; i < branch.size(); ++i)
            {
                const auto copy = copies.at(branch[i]);
                if (i && !batch.setParent(copy, copies.at(scene.entities().try_get(branch[i])->parent))) return false;
                for (const auto& component : components[i])
                {
                    const auto* info = core::reflection::ComponentRegistry::get(component.family);
                    core::JsonArchiveReader reader(component.value);
                    if (!core::reflection::load(reader, loading, *info->metaType, batch.addComponent(copy, component.family))) return false;
                }
            }
            const auto parent = scene.entities().try_get(root)->parent;
            if (!loading.finishLoad() || !scene.append(std::move(batch)) || !scene.setParent(copies.at(root), parent)) return false;
        }
        else
        {
            scene.destroyEntity(root); candidate->settle();
            std::erase_if(keys, [&](const auto& entry) { return !scene.entities().contains(entry.second); });
        }
        if (!_storage.snapshot(scene, after->archive))
        {
            _error = "Cannot save the changed branch; check references to deleted objects.";
            return false;
        }
        for (const auto& entity : scene.entities())
        {
            if (entity.id == core::EntityManager::ROOT_PARENT) continue;
            for (const auto& [entityKey, id] : keys) if (id == entity.id) { after->keys.push_back(entityKey); break; }
        }
        // Validate the complete result before committing; dangling required
        // references must not leave the live document partially changed.
        auto validation = std::make_unique<core::SceneInstance>();
        if (!_storage.restore(after->archive, validation->scene())) return false;
        auto command = std::make_unique<FunctionCommand>(duplicate ? "Duplicate object" : "Delete object",
            [this, after, copyKey](EditContext&) { if (!restore(*after)) return false; _createdKey = copyKey; return true; },
            [this, before, key](EditContext&) { if (!restore(*before)) return false; _createdKey = key; return true; });
        auto context = editContext();
        return _history.execute(std::move(command), context);
    }

    bool SceneSession::play()
    {
        if (_runtime) return true;
        std::string archive;
        if (!_storage.snapshot(editorScene(), archive)) return false;
        auto candidate = std::make_unique<core::SceneInstance>();
        if (!_storage.restore(archive, candidate->scene())) return false;
        candidate->settle();
        _runtime = std::move(candidate);
        try { rebind(); }
        catch (...)
        {
            // Keep failed runtime alive until all systems have detached from it.
            candidate = std::move(_runtime); rebind(); throw;
        }
        return true;
    }

    bool SceneSession::stop()
    {
        if (!_runtime) return true;
        auto previous = std::move(_runtime);
        try { rebind(); } // detach before the previous instance is destroyed
        catch (...) { _runtime = std::move(previous); rebind(); throw; }
        return true;
    }

    bool SceneSession::apply(const SceneRequest& request)
    {
        if (request.document != _document) { _error = "Request belongs to a closed document."; return false; }
        if (request.operation == SceneOperation::Stop) return stop();
        if (playing()) { _error = "Stop simulation before editing the scene."; return false; }
        if (request.operation == SceneOperation::CancelEdit) return cancelEdit();
        finishEdit();
        switch (request.operation)
        {
        case SceneOperation::Open: return open(request.path, request.discardChanges);
        case SceneOperation::Reload: return open(_path, request.discardChanges);
        case SceneOperation::Import: return import(request.path);
        case SceneOperation::Save: return save(_path);
        case SceneOperation::SaveAs: return save(request.path);
        case SceneOperation::Play: return play();
        case SceneOperation::CreateEntity: return create(request.position);
        case SceneOperation::AddComponent: return changeComponent(request.entity, request.component, true);
        case SceneOperation::RemoveComponent: return changeComponent(request.entity, request.component, false);
        case SceneOperation::DuplicateEntity: return changeBranch(request.entity, true);
        case SceneOperation::DeleteEntity: return changeBranch(request.entity, false);
        case SceneOperation::Undo: { auto context = editContext(); return _history.undo(context); }
        case SceneOperation::Redo: { auto context = editContext(); return _history.redo(context); }
        default: return false;
        }
    }

    void SceneSession::processRequests()
    {
        while (!_requests.empty())
        {
            auto request = std::move(_requests.front()); _requests.pop_front();
            _error.clear();
            try
            {
                if (!apply(request) && _error.empty()) _error = "Scene operation failed; previous state retained.";
                settle();
            }
            catch (const std::exception& error) { _error = error.what(); }
            if (!_error.empty())
            {
                log::error("Scene workflow: {}", _error);
                // Dependent requests (e.g. Save then Open) cannot run after failure.
                _requests.clear(); break;
            }
        }
    }
}
