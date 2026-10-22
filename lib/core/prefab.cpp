#include "pch.h"
#include "prefab.h"
#include "core/meta.h"
#include "scene.h"
#include "transform.h"
#include "cstdmf/log.h"
#include <mutex>
#include <unordered_set>

namespace csyren::core
{
    void PrefabRef::describe()
    {
        // Archived as the plain path ("prefabs/grenade.prefab"); empty means none.
        reflection::MetaFactory<PrefabRef>{}.primitiveType("PrefabRef"_hs)
            .archive({
                [](ArchiveWriter& writer, SerializationContext&, const reflection::MetaType&, const void* value) -> bool {
                    return writer.string(static_cast<const PrefabRef*>(value)->path);
                },
                [](ArchiveReader& reader, SerializationContext&, const reflection::MetaType&, void* value) -> bool {
                    return reader.string(static_cast<PrefabRef*>(value)->path);
                } });
    }

    void PrefabLibrary::setLoader(Loader loader)
    {
        std::lock_guard lock(_mutex);
        _loader = std::move(loader);
        _loaded.clear(); _failed.clear();
    }
    std::string normalizePrefabPath(std::string path)
    {
        for (auto& c : path) if (c == '\\') c = '/';
        while (path.rfind("./", 0) == 0) path.erase(0, 2);
        if (path.rfind("assets/", 0) == 0) path.erase(0, 7);
        return path;
    }

    PrefabHandle PrefabLibrary::load(std::string_view path)
    {
        const auto key = normalizePrefabPath(std::string(path));
        Loader loader;
        {
            std::lock_guard lock(_mutex);
            if (auto it = _loaded.find(key); it != _loaded.end()) return it->second;
            if (_failed.contains(key)) return nullptr;
            loader = _loader;
        }
        std::string error;
        auto prefab = loader ? loader(key, error) : nullptr;
        if (!loader) error = "no prefab loader is installed";
        {
            std::lock_guard lock(_mutex);
            if (!prefab) {
                if (_failed.insert(key).second) log::warning("Prefab '{}' cannot be loaded: {}", key, error.empty() ? "unknown error" : error);
                return nullptr;
            }
            // Another thread may have loaded it meanwhile: keep the first copy.
            const auto [it, inserted] = _loaded.try_emplace(key, prefab);
            if (!inserted) return it->second;
        }
        // Cached before its references are loaded, so references in a cycle stop here.
        for (const auto& reference : prefab->references())
            if (!reference.empty()) (void)load(reference);
        return prefab;
    }
    void PrefabLibrary::invalidate(std::string_view path)
    {
        std::lock_guard lock(_mutex);
        const auto key = normalizePrefabPath(std::string(path));
        _loaded.erase(key); _failed.erase(key);
    }
    void PrefabLibrary::clear()
    {
        std::lock_guard lock(_mutex);
        _loaded.clear(); _failed.clear();
    }

    void logOnce(const std::string& key, const std::string& message)
    {
        static std::mutex mutex;
        static std::unordered_set<std::string> logged;
        {
            std::lock_guard lock(mutex);
            if (!logged.insert(key).second) return;
        }
        log::warning("{}", message);
    }

    Entity::ID finishInstance(Scene& scene, EntityBatch&& batch, Entity::ID root, const SpawnPose& pose,
        const std::vector<PrefabEdit>& edits, const Prefab& prefab)
    {
        auto* transform = batch.getComponent<Transform>(root);
        if (!transform) transform = batch.addComponent<Transform>(root);
        if (transform) {
            transform->position = pose.position;
            transform->rotation = pose.rotation;
            if (pose.scale) transform->scale = *pose.scale;
        } else logOnce(prefab.path() + "#transform", "Prefab '" + prefab.path() + "': cannot place the copy (Transform is not registered)");
        for (const auto& edit : edits) if (edit) edit(batch, root, prefab);
        if (!scene.append(std::move(batch))) {
            log::error("Prefab '{}': the copy could not be added to the scene", prefab.path());
            return Entity::invalidID;
        }
        if (pose.parent != EntityManager::ROOT_PARENT && !scene.setParent(root, pose.parent))
            logOnce(prefab.path() + "#parent", "Prefab '" + prefab.path() + "': spawn parent does not exist, the copy stays at the root");
        return root;
    }
}
