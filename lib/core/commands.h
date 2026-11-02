#pragma once
// core::Commands: execute() parameter that changes the composition of the scene,
// that is which entities and components exist.
//
//   class LauncherSystem : public core::TypedSystem<LauncherSystem> {
//   public:
//       void execute(core::Query<Launcher, const core::Transform> launchers, core::Commands commands) {
//           for (auto [id, launcher, tr] : launchers) {
//               launcher.last = commands.spawn(launcher.projectile, { tr.position, tr.rotation })
//                   .edit<physics::RigidBody>([v = launcher.velocity](physics::RigidBody& body) { body.linearVelocity = v; })
//                   .with(Projectile{ .owner = id })
//                   .id();
//               if (--launcher.shots == 0) commands.entity(id).remove<Launcher>();
//           }
//       }
//   };
//
//   create(name, parent)   a new entity; .with(T{...}) gives it components.
//   spawn(prefab, pose)    a copy of a prefab (core/prefab.h); .with(T{...}) adds T
//                          to the copy's root or replaces the prefab's T, .edit<T>(fn)
//                          changes the prefab's T on the root.
//   .id()                  the ID of the new entity (of the copy's root), known now.
//                          The entity exists after the commit; if it cannot be
//                          created, the ID never names an entity.
//   entity(id)             an existing entity: .with(T{...}) adds T when the entity
//                          has none (an existing T keeps its value), .remove<T>(),
//                          .destroy() (with its children).
//
// Commands change composition only. Values are given only to new entities and
// new components; changing the value of an existing component is a write and
// needs that component in a Query.
//
// The commands are applied in this task's structural commit, in the order of the
// calls (create, spawn and entity), so an ID from create(...).id() can be used by
// a later command, even one in the same statement. Nothing is visible before the
// commit, not even to this execute(): its queries keep seeing the scene as it
// was. Systems ordered after this one and systems of later phases see the
// changes in the same frame. Commands declares no component types, so it
// conflicts with no reader or writer; two structural tasks are not ordered
// against each other (only their commits are): use `after` when one must see
// the other's changes.
//
// What is chained to a command (with, edit, remove, ...) belongs to it, whenever
// the builder is used during execute(). Builders cannot be copied or moved; one
// left by an exception is dropped and gives its ID back. Lambdas given to edit
// run in the commit, after execute() returned: capture by value, never references
// to locals or query rows. Commands itself cannot be copied or kept: pass it on
// by reference.
//
// Outside a graph task (a system run directly, a Commands made with makeParam)
// the commands are applied when the Commands object goes away (after execute()),
// followed by Scene::flush().
//
// An empty PrefabRef spawns nothing (id() is invalidID). A prefab that cannot be
// loaded, an edit of a component the prefab's root lacks, or a type that is not
// a registered component is logged once and skipped. A command for an entity
// that is gone by the commit does nothing.
#include "typed_system.h"
#include "prefab.h"
#include "entity_batch.h"
#include "entity_ref.h"
#include "component_registry.h"
#include "cstdmf/log.h"
#include <exception>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

namespace csyren::core
{
    class Commands
    {
        // One command. Records are applied in the order they were made.
        struct Record
        {
            virtual ~Record() = default;
            virtual void commit(Scene& scene) = 0;
            virtual void cancel(Scene&) {} // gives a reserved ID back; harmless after a commit
            bool dropped = false; // its builder was left by an exception
            bool done = false;    // committed or cancelled
        };

        // The builder of one command: what is chained to it is added to its record.
        template<class R> class Builder
        {
        public:
            Builder(const Builder&) = delete;
            Builder& operator=(const Builder&) = delete;
            Builder(Builder&&) = delete;
            Builder& operator=(Builder&&) = delete;
        protected:
            Builder(Scene& scene, std::shared_ptr<R> record)
                : _scene(&scene), _record(std::move(record)), _exceptions(std::uncaught_exceptions()) {}
            ~Builder()
            {
                if (std::uncaught_exceptions() <= _exceptions) return;
                _record->dropped = true;
                _record->cancel(*_scene);
            }
            Scene* _scene;
            std::shared_ptr<R> _record;
            int _exceptions;
        };

        using BatchEdit = std::function<void(EntityBatch&, Entity::ID)>;
        template<class T> static void requireComponentValue()
        {
            static_assert(!std::is_const_v<T> && !std::is_reference_v<T>, "with<T>: T is a plain component type");
            static_assert(std::is_copy_constructible_v<T>, "with<T>: T is a copyable component");
        }
        // with(T) on an entity of a batch: adds T or replaces the value given before.
        template<class T> static bool setInBatch(EntityBatch& batch, Entity::ID id, const T& value)
        {
            if (auto* existing = batch.getComponent<T>(id)) { *existing = value; return true; }
            if (auto* added = batch.addComponent<T>(id)) { *added = value; return true; }
            return false;
        }
        template<class T> static std::string notRegistered(std::string_view where, std::string_view what = "with")
        {
            return std::string(where) + ": " + std::string(what) + "<" + std::string(typeName<T>()) + ">: not a registered component";
        }

        struct CreateRecord final : Record
        {
            std::string name;
            Entity::ID parent = EntityManager::ROOT_PARENT;
            Entity::ID id = Entity::invalidID; // reserved when the command is made
            std::vector<BatchEdit> components;
            void cancel(Scene& scene) override { scene.releaseEntityId(id); }
            void commit(Scene& scene) override
            {
                auto batch = scene.stageEntities(1, id);
                const auto created = batch.createEntity(name);
                for (const auto& add : components) add(batch, created);
                if (!scene.append(std::move(batch)))
                {
                    log::error("Commands: entity '{}' could not be created", name);
                    cancel(scene);
                    return;
                }
                if (parent != EntityManager::ROOT_PARENT && !scene.setParent(created, parent))
                    logOnce("#create-parent:" + name, "Commands: the parent of '" + name + "' does not exist, the entity stays at the root");
            }
        };
        struct SpawnRecord final : Record
        {
            PrefabHandle prefab;
            std::string path;
            SpawnPose pose;
            std::vector<PrefabEdit> edits;
            Entity::ID id = Entity::invalidID; // reserved when the command is made; none for an empty reference
            void cancel(Scene& scene) override { if (id != Entity::invalidID) scene.releaseEntityId(id); }
            void commit(Scene& scene) override
            {
                if (id == Entity::invalidID) return; // reference not set: nothing to spawn
                auto loaded = prefab;
                if (!loaded) {
                    auto* library = Services::find<PrefabLibrary>();
                    if (!library) logOnce("#no-library", "Spawn of '" + path + "': no PrefabLibrary service is registered");
                    else loaded = library->load(path); // logs a failure once per path
                }
                const auto root = loaded ? loaded->instantiate(scene, pose, edits, id) : Entity::invalidID;
                if (root != id) cancel(scene); // not created: the ID stays unused
            }
        };
        struct EntityRecord final : Record
        {
            Entity::ID id = Entity::invalidID;
            std::vector<std::function<void(Scene&, Entity::ID)>> changes;
            void commit(Scene& scene) override { for (const auto& change : changes) change(scene, id); }
        };

        // Applies the records in order. If one throws, the rest are cancelled.
        static void apply(Scene& scene, const std::vector<std::shared_ptr<Record>>& records)
        {
            for (size_t i = 0; i < records.size(); ++i)
            {
                auto& record = *records[i];
                if (record.done) continue;
                record.done = true;
                if (record.dropped) continue; // gave its ID back when it was dropped
                try { record.commit(scene); }
                catch (...)
                {
                    record.cancel(scene);
                    cancel(scene, records);
                    throw;
                }
            }
        }
        static void cancel(Scene& scene, const std::vector<std::shared_ptr<Record>>& records) noexcept
        {
            for (const auto& record : records)
                if (!record->done) { record->done = true; record->cancel(scene); }
        }

    public:
        // A new entity (create).
        class CreateCommand : public Builder<CreateRecord>
        {
        public:
            template<class T> CreateCommand& with(T value)
            {
                requireComponentValue<T>();
                _record->components.emplace_back([value = std::move(value), name = _record->name](EntityBatch& batch, Entity::ID id) {
                    if (!setInBatch(batch, id, value)) logOnce("#create:" + std::string(typeName<T>()), notRegistered<T>("Commands: create '" + name + "'"));
                });
                return *this;
            }
            Entity::ID id() const noexcept { return _record->id; }
        private:
            friend class Commands;
            CreateCommand(Scene& scene, std::shared_ptr<CreateRecord> record) : Builder<CreateRecord>(scene, std::move(record)) {}
        };

        // A copy of a prefab (spawn).
        class SpawnCommand : public Builder<SpawnRecord>
        {
        public:
            template<class T> SpawnCommand& with(T value)
            {
                requireComponentValue<T>();
                _record->edits.emplace_back([value = std::move(value)](EntityBatch& batch, Entity::ID root, const Prefab& prefab) {
                    if (!setInBatch(batch, root, value))
                        logOnce(prefab.path() + "#with:" + std::string(typeName<T>()), notRegistered<T>("Spawn of '" + prefab.path() + "'"));
                });
                return *this;
            }
            template<class T, class Fn> SpawnCommand& edit(Fn&& fn)
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
            Entity::ID id() const noexcept { return _record->id; }
        private:
            friend class Commands;
            SpawnCommand(Scene& scene, std::shared_ptr<SpawnRecord> record) : Builder<SpawnRecord>(scene, std::move(record)) {}
        };

        // An existing entity (entity). A command for an entity that is gone by the
        // commit does nothing.
        class EntityCommands : public Builder<EntityRecord>
        {
        public:
            // Adds T when the entity has none; an existing T keeps its value.
            template<class T> EntityCommands& with(T value)
            {
                requireComponentValue<T>();
                _record->changes.emplace_back([value = std::move(value)](Scene& scene, Entity::ID id) {
                    if (!scene.entities().contains(id) || scene.hasComponent<T>(id)) return;
                    if (!reflection::ComponentRegistry::get(reflection::ComponentFamily::getID<T>()))
                    { logOnce("#with:" + std::string(typeName<T>()), notRegistered<T>("Commands: entity")); return; }
                    scene.addComponent<T>(id, value);
                });
                return *this;
            }
            template<class T> EntityCommands& remove()
            {
                static_assert(!std::is_const_v<T> && !std::is_reference_v<T>, "remove<T>: T is a plain component type");
                _record->changes.emplace_back([](Scene& scene, Entity::ID id) {
                    if (const auto* meta = reflection::ComponentRegistry::get(reflection::ComponentFamily::getID<T>()))
                        meta->destroy(&scene, id);
                    else logOnce("#remove:" + std::string(typeName<T>()), notRegistered<T>("Commands: entity", "remove"));
                });
                return *this;
            }
            // Destroys the entity and its children (at the end of the commit).
            EntityCommands& destroy()
            {
                _record->changes.emplace_back([](Scene& scene, Entity::ID id) { scene.destroyEntity(id); });
                return *this;
            }
            Entity::ID id() const noexcept { return _record->id; }
        private:
            friend class Commands;
            EntityCommands(Scene& scene, std::shared_ptr<EntityRecord> record) : Builder<EntityRecord>(scene, std::move(record)) {}
        };

        Commands(const Commands&) = delete;
        Commands& operator=(const Commands&) = delete;
        Commands(Commands&&) = delete;
        Commands& operator=(Commands&&) = delete;
        // Schedules the commands: into this task's commit, or applied now outside a
        // graph task. Left by an exception, it gives the reserved IDs back instead.
        ~Commands()
        {
            if (_records.empty()) return;
            auto records = std::make_shared<std::vector<std::shared_ptr<Record>>>(std::move(_records));
            auto* scene = _scene;
            if (std::uncaught_exceptions() > _exceptions) { cancel(*scene, *records); return; }
            try
            {
                if (scene->defersStructuralChanges())
                    scene->defer([scene, records] { apply(*scene, *records); },
                                 [scene, records] { cancel(*scene, *records); }); // a frame that never reached the commit
                else { apply(*scene, *records); scene->flush(); }
            }
            catch (const std::exception& error) { log::error("Commands could not be applied: {}", error.what()); cancel(*scene, *records); }
            catch (...) { log::error("Commands could not be applied"); cancel(*scene, *records); }
        }

        CreateCommand create(std::string_view name = EntityManager::DEFAULT_NAME,
            Entity::ID parent = EntityManager::ROOT_PARENT)
        {
            auto record = std::make_shared<CreateRecord>();
            record->name = std::string(name); record->parent = parent;
            record->id = _scene->reserveEntityId();
            _records.push_back(record);
            return CreateCommand(*_scene, std::move(record));
        }
        // A prefab referenced by a component field; loaded through PrefabLibrary at the commit.
        SpawnCommand spawn(const PrefabRef& prefab, const SpawnPose& pose = {})
        {
            auto record = std::make_shared<SpawnRecord>();
            record->path = prefab.path; record->pose = pose;
            if (!record->path.empty()) record->id = _scene->reserveEntityId();
            _records.push_back(record);
            return SpawnCommand(*_scene, std::move(record));
        }
        // A prefab loaded beforehand (e.g. in onAttach).
        SpawnCommand spawn(PrefabHandle prefab, const SpawnPose& pose = {})
        {
            auto record = std::make_shared<SpawnRecord>();
            record->prefab = std::move(prefab); record->pose = pose;
            if (record->prefab) record->id = _scene->reserveEntityId();
            _records.push_back(record);
            return SpawnCommand(*_scene, std::move(record));
        }
        EntityCommands entity(Entity::ID id)
        {
            auto record = std::make_shared<EntityRecord>();
            record->id = id;
            _records.push_back(record);
            return EntityCommands(*_scene, std::move(record));
        }
        EntityCommands entity(EntityRef reference) { return entity(reference.id); }

        // Parameter kind (see typed_system.h): structural, no component types.
        static void describeParam(ParamAccess& access) { access.structural = true; }
        static Commands makeParam(Scene& scene) { return Commands(scene); }

    private:
        explicit Commands(Scene& scene) : _scene(&scene), _exceptions(std::uncaught_exceptions()) {}
        Scene* _scene;
        std::vector<std::shared_ptr<Record>> _records;
        int _exceptions;
    };
}
