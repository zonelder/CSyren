#pragma once
#include "core/scene_context.h"
#include "scene_storage.h"
#include "editor_commands.h"
#include <deque>
#include <utility>
#include "math/Vector3.h"

namespace csyren::editor
{
    enum class SceneOperation { Open, Reload, Import, Save, SaveAs, Play, Stop, CreateEntity, Undo, Redo, CancelEdit, AddComponent, RemoveComponent, DuplicateEntity, DeleteEntity, ImportModel };
    struct SceneRequest
    {
        SceneOperation operation;
        uint64_t document;
        std::string path;
        bool discardChanges{};
        math::Vector3 position{};
        EntityKey entity{};
        size_t component{};
    };
    class SceneSession
    {
    public:
        using Bind = std::function<void(core::SceneContext, core::SceneContext, bool)>;
        explicit SceneSession(SceneStorage& storage);
        core::Scene& editorScene() const { return _editor->scene(); }
        core::SceneContext editorContext() const { return _editor->context(); }
        core::SceneContext activeContext() const { return (_runtime ? _runtime : _editor)->context(); }
        bool playing() const { return bool(_runtime); }
        uint64_t documentId() const { return _document; }
        const std::string& path() const { return _path; }
        const std::string& error() const { return _error; }
        bool dirty() const { return _history.dirty() || (_edit && _edit->changed()); }
        bool canUndo() const { return !playing() && (_history.canUndo() || (_edit && _edit->changed())); }
        bool canRedo() const { return !playing() && !_edit && _history.canRedo(); }
        void setBind(Bind bind) { _bind = std::move(bind); rebind(); }
        void enqueue(SceneOperation operation, std::string path = {}, bool discard = false)
        { _requests.push_back({ operation, _document, std::move(path), discard }); }
        void enqueue(SceneRequest request) { _requests.push_back(std::move(request)); }
        void processRequests(); // only at the frame boundary
        void settle();
        void finishEdit();
        bool cancelEdit();
        EntityKey keyFor(core::Entity::ID id);
        core::Entity::ID resolve(EntityKey key) const;
        core::Entity::ID takeCreatedEntity() { return resolve(std::exchange(_createdKey, 0)); }
        template<class T> void preview(EntityKey key, size_t family, const FieldPath& path,
            const T& before, const T& after, bool finished)
        {
            if (playing()) return;
            editorScene().patch(resolve(key), family);
            auto* edit = dynamic_cast<FieldEdit<T>*>(_edit.get());
            if (!edit || edit->key != key || edit->family != family || edit->path != path)
            {
                finishEdit();
                _edit = std::make_unique<FieldEdit<T>>(key, family, path, before, after);
            }
            else edit->after = after;
            if (finished) finishEdit();
        }
        // Same as preview() for editors that only know the reflected type.
        void previewArchived(EntityKey key, size_t family, const FieldPath& path, const core::reflection::MetaType& type,
            nlohmann::json before, nlohmann::json after, bool finished)
        {
            if (playing()) return;
            editorScene().patch(resolve(key), family);
            auto* edit = dynamic_cast<ArchivedFieldEdit*>(_edit.get());
            if (!edit || edit->key != key || edit->family != family || edit->path != path || edit->type != &type)
            {
                finishEdit();
                _edit = std::make_unique<ArchivedFieldEdit>(key, family, path, &type, std::move(before), std::move(after));
            }
            else edit->after = std::move(after);
            if (finished) finishEdit();
        }
    private:
        using Keys = std::vector<EntityKey>;
        struct Snapshot { std::string archive; Keys keys; };
        EditContext editContext() { return { editorScene(), _entities }; }
        void rebind();
        bool replaceEditor(std::unique_ptr<core::SceneInstance>& candidate, EntityMap keys);
        bool capture(Snapshot&);
        bool restore(const Snapshot&);
        EntityMap mapKeys(core::Scene&, const Keys&);
        bool open(const std::string&, bool discard);
        bool save(const std::string&);
        bool import(const std::string&);
        bool importModel(const std::string&);
        bool play();
        bool stop();
        bool create(const math::Vector3& position);
        bool changeComponent(EntityKey, size_t family, bool add);
        bool changeBranch(EntityKey, bool duplicate);
        bool apply(const SceneRequest&);
        SceneStorage& _storage;
        std::unique_ptr<core::SceneInstance> _editor, _runtime;
        EntityMap _entities;
        CommandHistory _history;
        std::unique_ptr<PendingFieldEdit> _edit;
        std::deque<SceneRequest> _requests;
        Bind _bind;
        std::string _path, _error;
        uint64_t _document{ 1 }, _nextKey{}, _createdKey{};
    };
}
