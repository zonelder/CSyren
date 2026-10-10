#pragma once
#include "entity_manager.h"
#include "component_pool.h"
#include "component_registry.h"
#include <utility>

namespace csyren::core
{
    class Scene;

    // Isolated data only: no systems, publishers, deferred commands or scene mutations.
    // Destination IDs are planned without modifying the scene allocator.
    class EntityBatch
    {
        friend class Scene;
        struct Components {
            const reflection::ComponentMeta* meta;
            std::shared_ptr<PoolBase> pool;
        };
        std::vector<Entity::ID> _planned;
        std::vector<Entity> _entities;
        std::unordered_map<Entity::ID, size_t> _indices;
        std::unordered_map<size_t, Components> _components;
        bool _consumed = false;
        bool appendTo(Scene&);
        explicit EntityBatch(std::vector<Entity::ID> planned) : _planned(std::move(planned))
        {
            _entities.reserve(_planned.size());
            _indices.reserve(_planned.size());
        }
    public:
        EntityBatch(const EntityBatch&) = delete;
        EntityBatch& operator=(const EntityBatch&) = delete;
        EntityBatch(EntityBatch&& other) noexcept :
            _planned(std::move(other._planned)), _entities(std::move(other._entities)),
            _indices(std::move(other._indices)), _components(std::move(other._components)),
            _consumed(std::exchange(other._consumed, true)) {}
        EntityBatch& operator=(EntityBatch&&) = delete;

        Entity::ID createEntity(std::string_view name)
        {
            if (_consumed || _entities.size() >= _planned.size()) throw std::logic_error("EntityBatch has no available IDs");
            Entity entity;
            entity.id = _planned[_entities.size()];
            entity.parent = EntityManager::ROOT_PARENT;
            entity.name = name;
            _indices.emplace(entity.id, _entities.size());
            _entities.push_back(std::move(entity));
            return _entities.back().id;
        }
        bool setParent(Entity::ID id, Entity::ID parent)
        {
            auto child = _indices.find(id);
            if (_consumed || child == _indices.end() || (parent != EntityManager::ROOT_PARENT && !_indices.contains(parent))) return false;
            _entities[child->second].parent = parent;
            return true;
        }
        void* addComponent(Entity::ID id, size_t family)
        {
            if (_consumed) return nullptr;
            auto& entity = _entities.at(_indices.at(id));
            if (entity.has(family)) throw std::runtime_error("Duplicate imported component");
            const auto* meta = reflection::ComponentRegistry::get(family);
            if (!meta || !meta->createPool) return nullptr;
            auto it = _components.find(family);
            if (it == _components.end()) it = _components.emplace(family, Components{ meta, meta->createPool() }).first;
            void* value = it->second.pool->emplaceDefault(id);
            entity.add(family);
            return value;
        }
        template<class T> T* addComponent(Entity::ID id)
        {
            return static_cast<T*>(addComponent(id, reflection::ComponentFamily::getID<T>()));
        }
        // Linear hierarchy validation. No destination scene is consulted or modified.
        bool validateHierarchy() const
        {
            std::vector<uint8_t> marks(_entities.size());
            for (size_t start = 0; start < _entities.size(); ++start) {
                size_t i = start;
                while (marks[i] == 0) {
                    marks[i] = 1;
                    const auto parent = _entities[i].parent;
                    if (parent == EntityManager::ROOT_PARENT) break;
                    const auto it = _indices.find(parent);
                    if (it == _indices.end()) return false;
                    i = it->second;
                    if (marks[i] == 1) return false;
                }
                i = start;
                while (marks[i] == 1) {
                    marks[i] = 2;
                    const auto parent = _entities[i].parent;
                    if (parent == EntityManager::ROOT_PARENT) break;
                    i = _indices.at(parent);
                }
            }
            return true;
        }
        size_t size() const noexcept { return _entities.size(); }
        const Entity* entity(Entity::ID id) const noexcept
        {
            const auto it = _indices.find(id);
            return !_consumed && it != _indices.end() ? &_entities[it->second] : nullptr;
        }
        void* getComponent(Entity::ID id, size_t family) const noexcept
        {
            const auto it = _components.find(family);
            return !_consumed && it != _components.end() ? it->second.pool->raw(id) : nullptr;
        }
        template<class T> T* getComponent(Entity::ID id) const noexcept
        {
            return static_cast<T*>(getComponent(id, reflection::ComponentFamily::getID<T>()));
        }
    };
}
