#pragma once
#include  "cstdmf/log.h"

#include "entity.h"
#include "cstdmf/sparse_set.h"
#include "cstdmf/tree_algorithms.h"

#include <unordered_set>
#include <algorithm>
#include <mutex>


namespace csyren::core
{
    namespace details { struct EntityManagerTestAccess; }

    // Entities of a scene and their IDs. An ID is a slot plus the slot's generation
    // (see Entity::ID): freeing a slot raises its generation, so IDs of destroyed
    // entities stay invalid when the slot is reused. A slot whose generation runs
    // out is retired (never handed out again).
    class EntityManager
    {
        friend class EntityBatch;
        friend struct details::EntityManagerTestAccess;
    public:
        static constexpr std::string_view DEFAULT_NAME{ "new Entity" };
        static constexpr Entity::ID ROOT_PARENT = 0;

        void init()
        {
            _entities.emplace(ROOT_PARENT, Entity{});
            Entity* ent = _entities.try_get(ROOT_PARENT);
            ent->id = ROOT_PARENT;
            ent->parent = Entity::invalidID;
            ent->name = "scene root";
        }

        [[nodiscard]] Entity::ID createEntity(std::string_view name = DEFAULT_NAME,
            Entity::ID parent = ROOT_PARENT)
        {
            return createReservedEntity(reserveId(), name, parent);
        }

        // Reservation only touches the allocator, never the entity storage. A
        // reserved ID becomes an entity through createReservedEntity() or as the
        // root of an EntityBatch, or is given back with releaseReserved(). Thread safe.
        Entity::ID reserveId() {
            std::lock_guard lock(_idMutex);
            const auto id = allocateId();
            _reserved.insert(id);
            return id;
        }
        // The slot goes back with the next generation: `id` never becomes an entity.
        void releaseReserved(Entity::ID id) {
            std::lock_guard lock(_idMutex);
            if (_reserved.erase(id)) freeSlot(id);
        }
        bool isReserved(Entity::ID id) {
            std::lock_guard lock(_idMutex);
            return _reserved.contains(id);
        }

        Entity::ID createReservedEntity(Entity::ID id, std::string_view name,
            Entity::ID parent = ROOT_PARENT)
        {
            {
                std::lock_guard lock(_idMutex);
                _reserved.erase(id);
            }
            if (isPendingDestroy(parent))
            {
                log::error("Cannot create entity under pending-destroy parent {}", parent);
                parent = ROOT_PARENT;
            }



            if (!canBeParent(parent, id))
            {
                log::error("EntityManager::createEntity: cycle detected. parent={} child={}. "
                    "Falling back to scene root", parent, id);
                parent = ROOT_PARENT;
            }

            _entities.emplace(id, Entity{});
            Entity* ent = _entities.try_get(id);
            Entity* p = _entities.try_get(parent);
            if (!p)
            {
                log::error("EntityManager::createEntity: parent id={} not found, "
                    "adding '{}' to scene root", parent, name);
                p = _entities.try_get(ROOT_PARENT);
            }

            ent->id = id;
            ent->parent = p->id;
            ent->name = name;
            p->children.push_back(id);
            return id;
        }

        [[nodiscard]] Entity::ID createEntity(Entity::ID parent)
        {
            return createEntity(DEFAULT_NAME, parent);
        }

        void queueDestroy(Entity::ID id)
        {
            if (id == ROOT_PARENT || !_entities.contains(id)) return;
            _pendingDestroy.insert(id);
        }

        [[nodiscard]] std::vector<Entity::ID> collectDestroyList() const
        {
            std::vector<Entity::ID> result;
            for (auto root : _pendingDestroy)
                cstdmf::tree::preorder(root,
                    [this](auto id) -> const auto& { return tryGet(id)->children; },
                    [&](auto id) { result.push_back(id); });

            std::sort(result.begin(), result.end());
            result.erase(std::unique(result.begin(), result.end()), result.end());

            return result;
        }

        [[nodiscard]] bool isPendingDestroy(Entity::ID id) const
        {
            return std::find(_pendingDestroy.begin(), _pendingDestroy.end(), id) != _pendingDestroy.end();
        }


        bool setName(Entity::ID id, std::string_view newName)
        {
            Entity* entity = _entities.try_get(id);
            if (!entity)
            {
                log::error("EntityManager::setName: entity {} does not exist", id);
                return false;
            }
            if (id == ROOT_PARENT)
            {
                log::error("EntityManager::setName: cannot rename ROOT_PARENT");
                return false;
            }
            entity->name = newName;
            return true;
        }

        [[nodiscard]] Entity* tryGet(Entity::ID id) { return _entities.try_get(id); }
        [[nodiscard]] const Entity* tryGet(Entity::ID id) const { return _entities.try_get(id); }
        [[nodiscard]] bool contains(Entity::ID id) const { return _entities.contains(id); }
        // Read-only planning; no freelist copy and no ID reservation in the live scene.
        std::vector<Entity::ID> planAppend(size_t count) const
        {
            std::vector<Entity::ID> ids; ids.reserve(count);
            for (size_t i = 0; i < count; ++i) ids.push_back(plannedId(i));
            return ids;
        }
        bool setParent(Entity::ID id, Entity::ID parent)
        {
            if (id == ROOT_PARENT || isPendingDestroy(id) || isPendingDestroy(parent)) return false;
            return cstdmf::tree::reparent(id, parent, Entity::invalidID,
                [this](auto key) { return tryGet(key); }, &Entity::parent, &Entity::children);
        }
        [[nodiscard]] const EntitySet<Entity>& all() const { return _entities; }

        void finalizeDestroy(Entity::ID id)
        {

            Entity* ent = _entities.try_get(id);
            if (!ent) return;

            if (ent->parent != Entity::invalidID)
            {
                if (Entity* p = _entities.try_get(ent->parent))
                {
                    auto& vec = p->children;
                    vec.erase(std::remove(vec.begin(), vec.end(), id), vec.end());
                }
            }

            for (auto& child : ent->children)
            {
                if (Entity* pChild = _entities.try_get(child))
                    pChild->parent = Entity::invalidID;
                else
                    log::error("EntityManager: child {} referenced but missing", child);
            }

            _entities.erase(id);

            std::lock_guard lock(_idMutex);
            freeSlot(id);
        }

        void clearPending() { _pendingDestroy.clear(); }

    private:
        // Slots below this limit can be handed out (the all-ones ID is invalidID).
        static constexpr uint32_t kSlotLimit = std::numeric_limits<uint32_t>::max();
        // Freeing a slot at this generation retires it.
        static constexpr uint32_t kLastGeneration = std::numeric_limits<uint32_t>::max() - 1;

        uint32_t generationOf(uint32_t slot) const noexcept
        {
            return slot < _generations.size() ? _generations[slot] : 0;
        }
        Entity::ID plannedId(size_t index) const
        {
            if (index < _freeSlots.size())
            {
                const auto slot = _freeSlots[_freeSlots.size() - index - 1];
                return Entity::makeID(slot, generationOf(slot));
            }
            const auto offset = index - _freeSlots.size();
            if (offset >= uint64_t(kSlotLimit) - _nextSlot) throw std::runtime_error("EntityManager: out of Entity IDs");
            return Entity::makeID(_nextSlot + static_cast<uint32_t>(offset), 0);
        }
        Entity::ID allocateId()
        {
            if (!_freeSlots.empty())
            {
                const auto slot = _freeSlots.back();
                _freeSlots.pop_back();
                return Entity::makeID(slot, generationOf(slot));
            }
            if (_nextSlot == kSlotLimit)
                throw std::runtime_error("EntityManager: out of Entity IDs");
            return Entity::makeID(_nextSlot++, 0);
        }
        // The slot of `id` goes back to the free list with the next generation,
        // or is retired when its generation is used up. Ignores stale IDs.
        void freeSlot(Entity::ID id)
        {
            const auto slot = Entity::slot(id);
            if (Entity::generation(id) != generationOf(slot)) return;
            if (slot >= _generations.size()) _generations.resize(size_t(slot) + 1, 0);
            if (_generations[slot] == kLastGeneration) return;
            ++_generations[slot];
            _freeSlots.push_back(slot);
        }

        bool canBeParent(Entity::ID parent, Entity::ID child) const
        {
            if (parent == ROOT_PARENT) return true;
            if (parent == child) return false;
            return !isAncestor(child, parent);
        }

        bool isAncestor(Entity::ID ancestor, Entity::ID descendant) const
        {
            return cstdmf::tree::isAncestor(ancestor, descendant, Entity::invalidID, [this](auto id) {
                const auto* e = tryGet(id); return e ? e->parent : Entity::invalidID;
            });
        }

        EntitySet<Entity>     _entities;
        std::vector<uint32_t> _generations;   // by slot; slots past the end are at generation 0
        std::vector<uint32_t> _freeSlots;     // reused last in, first out
        uint32_t              _nextSlot = 1;  // slot 0 is the scene root
        std::unordered_set<Entity::ID> _reserved; // reserved, not created yet
        std::mutex _idMutex;
        std::unordered_set<Entity::ID>   _pendingDestroy;
    };
}
