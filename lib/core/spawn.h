#pragma once
// core::Spawn: execute() parameter that creates copies of prefabs.
//
//   class LauncherSystem : public core::TypedSystem<LauncherSystem> {
//   public:
//       void execute(core::Update<Launcher, const core::Transform> launchers, core::Spawn spawn) {
//           for (auto [id, launcher, tr] : launchers)
//               launcher.last = spawn(launcher.projectile, { tr.position, tr.rotation })
//                   .edit<physics::RigidBody>([velocity = launcher.velocity](physics::RigidBody& body) { body.linearVelocity = velocity; })
//                   .with(Projectile{ .owner = id })
//                   .root();
//       }
//   };
//
// A copy becomes part of the scene in this task's structural commit, like an
// Insert: a system ordered after the spawner sees it in the same frame. Spawn
// declares no component types: nobody can see a copy before the commit, so the
// spawner conflicts with no reader or writer and spawners run in parallel (the
// commits are exclusive, as for Insert and Delete).
//
//   with(T value)     add T to the copy's root, or replace the prefab's T.
//   edit<T>(fn)       change the prefab's T on the root before anyone sees it.
//                     fn runs in the commit, after execute() returned: capture
//                     by value, never references to locals or query rows.
//   root()            the ID the copy's root will have, known now (it can be kept,
//                     e.g. in an EntityRef). The entity exists after the commit;
//                     if the copy cannot be created, the ID never names an entity.
//
// An empty PrefabRef spawns nothing (root() is invalidID). A prefab that cannot
// be loaded, or an edit of a component the prefab's root lacks, is logged once
// and skipped.
#include "typed_system.h"
#include "prefab.h"
#include "entity_batch.h"
#include "cstdmf/log.h"
#include <exception>
#include <memory>
#include <type_traits>
#include <utility>

namespace csyren::core
{
    class Spawn
    {
        struct Record
        {
            PrefabHandle prefab;
            std::string path;
            SpawnPose pose;
            std::vector<PrefabEdit> edits;
            Entity::ID root = Entity::invalidID; // reserved when the request is made
        };
    public:
        // One spawn. It is scheduled when the Request goes away (normally at the end
        // of the statement), so with/edit chained to it always apply. It cannot be
        // copied or moved, so it cannot outlive execute(); a Request destroyed by an
        // exception schedules nothing.
        class Request
        {
        public:
            Request(const Request&) = delete;
            Request& operator=(const Request&) = delete;
            Request(Request&&) = delete;
            Request& operator=(Request&&) = delete;
            ~Request()
            {
                if (!_scene || !_record) return;
                const auto root = _record->root;
                if (std::uncaught_exceptions() > _exceptions) { _scene->releaseEntityId(root); return; }
                try { schedule(*_scene, std::move(_record)); return; }
                catch (const std::exception& error) { log::error("Spawn could not be scheduled: {}", error.what()); }
                catch (...) { log::error("Spawn could not be scheduled"); }
                _scene->releaseEntityId(root);
            }

            // The ID of the copy's root; the entity exists after the commit.
            Entity::ID root() const noexcept { return _record ? _record->root : Entity::invalidID; }

            template<class T> Request& with(T value)
            {
                static_assert(!std::is_const_v<T> && std::is_copy_constructible_v<T>, "with<T>: T is a copyable component");
                _record->edits.emplace_back([value = std::move(value)](EntityBatch& batch, Entity::ID root, const Prefab& prefab) {
                    if (auto* existing = batch.getComponent<T>(root)) { *existing = value; return; }
                    if (auto* added = batch.addComponent<T>(root)) { *added = value; return; }
                    logOnce(prefab.path() + "#with:" + std::string(typeName<T>()),
                        "Spawn of '" + prefab.path() + "': with<" + std::string(typeName<T>()) + ">: not a registered component");
                });
                return *this;
            }
            template<class T, class Fn> Request& edit(Fn&& fn)
            {
                static_assert(!std::is_const_v<T>, "edit<T>: T is the component to change");
                static_assert(std::is_invocable_v<Fn&, T&>, "edit<T>(fn): fn takes T&");
                _record->edits.emplace_back([fn = std::forward<Fn>(fn)](EntityBatch& batch, Entity::ID root, const Prefab& prefab) mutable {
                    if (auto* component = batch.getComponent<T>(root)) { fn(*component); return; }
                    logOnce(prefab.path() + "#edit:" + std::string(typeName<T>()),
                        "Spawn of '" + prefab.path() + "': edit<" + std::string(typeName<T>()) + ">: the prefab root has no such component");
                });
                return *this;
            }
        private:
            friend class Spawn;
            Request(Scene& scene, std::shared_ptr<Record> record)
                : _scene(&scene), _record(std::move(record)), _exceptions(std::uncaught_exceptions()) {}
            Scene* _scene;
            std::shared_ptr<Record> _record;
            int _exceptions;
        };

        // A prefab referenced by a component field; loaded through PrefabLibrary at the commit.
        Request operator()(const PrefabRef& prefab, const SpawnPose& pose = {}) const
        {
            auto record = std::make_shared<Record>();
            record->path = prefab.path; record->pose = pose;
            if (!record->path.empty()) record->root = _scene->reserveEntityId();
            return Request(*_scene, std::move(record));
        }
        // A prefab loaded beforehand (e.g. in onAttach).
        Request operator()(PrefabHandle prefab, const SpawnPose& pose = {}) const
        {
            auto record = std::make_shared<Record>();
            record->prefab = std::move(prefab); record->pose = pose;
            if (record->prefab) record->root = _scene->reserveEntityId();
            return Request(*_scene, std::move(record));
        }

        // Parameter kind (see typed_system.h): structural, no component types.
        static void describeParam(ParamAccess& access) { access.structural = true; }
        static Spawn makeParam(Scene& scene) { return Spawn(scene); }

    private:
        explicit Spawn(Scene& scene) : _scene(&scene) {}
        // Runs in the task's structural commit (immediately outside a graph task).
        static void schedule(Scene& scene, std::shared_ptr<Record> record)
        {
            scene.defer([scene = &scene, record = std::move(record)] { commit(*scene, *record); });
        }
        static void commit(Scene& scene, const Record& record)
        {
            if (record.root == Entity::invalidID) return; // reference not set: nothing to spawn
            auto prefab = record.prefab;
            if (!prefab) {
                auto* library = Services::find<PrefabLibrary>();
                if (!library) logOnce("#no-library", "Spawn of '" + record.path + "': no PrefabLibrary service is registered");
                else prefab = library->load(record.path); // logs a failure once per path
            }
            const auto root = prefab ? prefab->instantiate(scene, record.pose, record.edits, record.root) : Entity::invalidID;
            if (root != record.root) scene.releaseEntityId(record.root); // not created: the ID stays unused
        }
        Scene* _scene;
    };
}
