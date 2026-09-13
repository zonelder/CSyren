#pragma once
#include  "cstdmf/log.h"

#include "entity.h"
#include "cstdmf/sparse_set.h"

#include <unordered_set>
#include <algorithm>


namespace csyren::core
{
    class EntityManager
    {
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
            if (isPendingDestroy(parent))
            {
                log::error("Cannot create entity under pending-destroy parent {}", parent);
                parent = ROOT_PARENT;
            }
            Entity::ID id = allocateId();


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
            if (!_entities.contains(id)) return;
            _pendingDestroy.insert(id);
        }

        [[nodiscard]] std::vector<Entity::ID> collectDestroyList() const
        {
            std::vector<Entity::ID> result;
            std::vector<Entity::ID> stack;

            for (Entity::ID rootId : _pendingDestroy)
                stack.push_back(rootId);

            while (!stack.empty())
            {
                Entity::ID current = stack.back();
                stack.pop_back();

                const Entity* ent = _entities.try_get(current);
                if (!ent) continue;

                result.push_back(current);
                for (Entity::ID child : ent->children)
                    stack.push_back(child);
            }

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
            entity->name = generateUniqueName(newName, entity->parent, id);
            return true;
        }

        [[nodiscard]] Entity* tryGet(Entity::ID id) { return _entities.try_get(id); }
        [[nodiscard]] const Entity* tryGet(Entity::ID id) const { return _entities.try_get(id); }
        [[nodiscard]] bool contains(Entity::ID id) const { return _entities.contains(id); }
        [[nodiscard]] const cstdmf::SparseSet<Entity>& all() const { return _entities; }

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

            if (id + 1 == _nextId)
                --_nextId;
            else
                _freeIDs.push_back(id);
        }

        void clearPending() { _pendingDestroy.clear(); }

    private:
        Entity::ID allocateId()
        {
            if (!_freeIDs.empty())
            {
                Entity::ID id = _freeIDs.back();
                _freeIDs.pop_back();
                return id;
            }
            if (_nextId == Entity::invalidID)
                throw std::runtime_error("EntityManager: out of Entity IDs");
            return _nextId++;
        }

        bool canBeParent(Entity::ID parent, Entity::ID child) const
        {
            if (parent == ROOT_PARENT) return true;
            if (parent == child) return false;
            return !isAncestor(child, parent);
        }

        bool isAncestor(Entity::ID ancestor, Entity::ID descendant) const
        {
            Entity::ID current = descendant;
            while (current != Entity::invalidID)
            {
                if (current == ancestor) return true;
                const Entity* e = _entities.try_get(current);
                if (!e) return false;
                current = e->parent;
            }
            return false;
        }

        cstdmf::SparseSet<Entity> _entities;
        std::vector<Entity::ID>   _freeIDs;
        Entity::ID                _nextId = 1;
        std::unordered_set<Entity::ID>   _pendingDestroy;
    };
}