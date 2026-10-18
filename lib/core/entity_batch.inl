#pragma once

namespace csyren::core
{
    inline bool Scene::append(EntityBatch&& data) { return data.appendTo(*this); }

    // All batch-specific preparation stays here, not in metadata, the event bus,
    // or normal entity/component operations. Requires exclusive scene access and
    // no EventBus publication/dispatch interleaved with this operation.
    inline bool EntityBatch::appendTo(Scene& scene)
    {
        auto& manager = scene._em;
        if (_consumed || !scene._bus || !scene._entityCreateToken.valid() ||
            !manager.contains(EntityManager::ROOT_PARENT) || !validateHierarchy()) return false;
        for (size_t i = 0; i < size(); ++i)
            if (_entities[i].id != manager.plannedId(i)) return false;
        for (const auto& [family, components] : _components)
            if (scene._cStates.contains(family) && !components.pool->canMove()) return false;

        size_t rootChildren = 0;
        for (auto& entity : _entities) entity.children.clear();
        for (const auto& entity : _entities) {
            if (entity.parent == EntityManager::ROOT_PARENT) ++rootChildren;
            else _entities[_indices.at(entity.parent)].children.push_back(entity.id);
        }
        manager._entities.prepareInsert(std::span<const Entity::ID>(_planned.data(), size()));
        auto& root = *manager.tryGet(EntityManager::ROOT_PARENT);
        root.children.reserve(root.children.size() + rootChildren);

        std::vector<size_t> addedFamilies; addedFamilies.reserve(_components.size());
        try {
            for (const auto& [family, components] : _components) {
                const bool adopt = !scene._cStates.contains(family);
                if (adopt) addedFamilies.push_back(family);
                components.meta->ensurePool(&scene);
                auto& destination = scene._cStates.at(family);
                if (!adopt) destination.pool->reserveKeys(components.pool->keys());
                if (!scene._bus->reservePending(destination.addToken, components.pool->keys().size()))
                    throw std::runtime_error("Entity batch publisher registration failed");
            }
            if (!scene._bus->reservePending(scene._entityCreateToken, size()))
                throw std::runtime_error("Entity batch publisher registration failed");
        } catch (...) {
            // Preflight can allocate; discard only the newly registered empty pools.
            // Existing values, allocator state and queued events have not changed.
            for (auto family : addedFamilies) {
                const auto it = scene._cStates.find(family);
                if (it == scene._cStates.end()) continue;
                scene._bus->unregister_publisher(it->second.addToken);
                scene._bus->unregister_publisher(it->second.removeToken);
                scene._bus->unregister_publisher(it->second.patchToken);
                scene._cStates.erase(it);
            }
            throw;
        }

        // Capacity and sparse pages are ready. From here, insertion and transfer
        // cannot allocate or throw (component relocation is checked above).
        static_assert(std::is_nothrow_move_constructible_v<Entity>);
        for (auto& entity : _entities) {
            const auto id = entity.id;
            const bool rootChild = entity.parent == EntityManager::ROOT_PARENT;
            (void)manager.allocateId();
            manager._entities.emplace(id, std::move(entity));
            if (rootChild) manager.tryGet(EntityManager::ROOT_PARENT)->children.push_back(id);
        }
        for (auto& [family, components] : _components) {
            auto& destination = scene._cStates.at(family).pool;
            if (std::find(addedFamilies.begin(), addedFamilies.end(), family) != addedFamilies.end())
                destination.swap(components.pool);
            else destination->moveFrom(*components.pool);
        }
        _consumed = true;
        if (size()) ++scene._hierarchyRevision;
        for (const auto& [family, components] : _components) ++scene._cStates.at(family).revision;
        for (size_t i = 0; i < size(); ++i) {
            const auto id = _planned[i];
            scene._bus->publish(scene._entityCreateToken, EntityCreateEvent{id});
            for (auto family : manager.tryGet(id)->componentView())
                scene._cStates.at(family).meta->publishAdded(&scene, id);
        }
        return true;
    }
}
