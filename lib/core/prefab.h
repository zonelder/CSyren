#pragma once
// Prefabs: an entity branch kept as an asset and copied into scenes.
//
//   PrefabRef      reference to a prefab file in a component field (path
//                  relative to the project's assets, "prefabs/grenade.prefab").
//                  An empty reference means "nothing to spawn".
//   Prefab         a loaded prefab; instantiate() creates a copy of its branch.
//   PrefabLibrary  service that loads prefabs by path and caches them (the
//                  runtime installs the file loader, source/runtime/prefab_file.h).
//   SpawnPose      where the copy goes: the root's position and rotation (and
//                  scale, when set) relative to `parent`.
//
// Systems create copies with the core::Spawn parameter (core/spawn.h); the
// editor and lifecycle code call Prefab::instantiate() between frames.
#include <functional>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <unordered_set>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "math/math.h"
#include "entity.h"
#include "entity_manager.h"
#include "registration.h"

namespace csyren::core
{
    class Scene;
    class EntityBatch;
    class Prefab;

    struct PrefabRef
    {
        std::string path;
        bool empty() const noexcept { return path.empty(); }
        bool operator==(const PrefabRef&) const = default;
        static void describe(); // archived as the path string (prefab.cpp)
    };
    CS_REFLECT(PrefabRef);

    struct SpawnPose
    {
        math::Vector3 position{ 0, 0, 0 };
        math::Quaternion rotation;              // identity
        std::optional<math::Vector3> scale;     // empty: the prefab's own scale
        Entity::ID parent = EntityManager::ROOT_PARENT;
    };

    // Change of the copy's root before the copy becomes part of the scene
    // (Spawn's with/edit). The prefab is passed for log messages.
    using PrefabEdit = std::function<void(EntityBatch&, Entity::ID root, const Prefab&)>;

    class Prefab
    {
    public:
        virtual ~Prefab() = default;
        virtual const std::string& path() const noexcept = 0;
        // Creates a copy of the branch in `scene`: the root gets the pose, then the
        // edits run, then the whole branch is appended. Structural: call it between
        // frames or from a structural commit (core::Spawn does). Returns the root,
        // or Entity::invalidID when the copy could not be created (logged).
        virtual Entity::ID instantiate(Scene& scene, const SpawnPose& pose, const std::vector<PrefabEdit>& edits = {}) const = 0;
        // Prefabs this one refers to (PrefabRef fields of its components); the
        // library loads them together with this one.
        virtual std::vector<std::string> references() const { return {}; }
    };
    using PrefabHandle = std::shared_ptr<const Prefab>;

    // "assets/x" and "x" (and backslash spellings) name the same prefab asset.
    std::string normalizePrefabPath(std::string path);

    // Service: prefabs by path, cached. Reading files is the loader's job (the
    // runtime installs one that reads .prefab documents); the library caches
    // results, reports a failing path once instead of on every spawn, and loads
    // the prefabs a loaded prefab refers to as well (no file reads on later spawns).
    class PrefabLibrary
    {
    public:
        // Returns the prefab, or null and a message in `error`.
        using Loader = std::function<PrefabHandle(const std::string& path, std::string& error)>;
        void setLoader(Loader loader);
        // Null when there is no loader or the prefab cannot be loaded (logged once per
        // path). Paths are normalized (normalizePrefabPath) before the cache lookup.
        PrefabHandle load(std::string_view path);
        // Forgets the cached prefab (or failure), e.g. after the file was saved.
        void invalidate(std::string_view path);
        void clear();
        void shutdown() { clear(); } // service shutdown: drop prefabs before the resources they use
    private:
        std::mutex _mutex;
        Loader _loader;
        std::unordered_map<std::string, PrefabHandle> _loaded;
        std::unordered_set<std::string> _failed;
    };

    // Shared by Prefab implementations: gives the batch's `root` the pose (adds a
    // Transform when the prefab root has none), runs the edits, appends the batch
    // and parents the root. Returns the root, or Entity::invalidID if the batch
    // could not be appended.
    Entity::ID finishInstance(Scene& scene, EntityBatch&& batch, Entity::ID root, const SpawnPose& pose,
        const std::vector<PrefabEdit>& edits, const Prefab& prefab);

    // Logs `message` once per key (prefab path + problem), for spawn errors that
    // would otherwise repeat every frame.
    void logOnce(const std::string& key, const std::string& message);
}
