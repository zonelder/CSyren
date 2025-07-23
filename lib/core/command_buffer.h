#ifndef __CSYREN_COMMAND_BUFFER__
#define __CSYREN_COMMAND_BUFFER__

#include "entity.h"
#include "component_base.h"

#include <mutex>

namespace csyren::core
{
    struct DestroyEntityCommand
    {
        Entity::ID id;
    };
    struct DestroyComponentCommand
    {
        Entity::ID   entt;
        size_t       family;
    };

    class DeferredCommands
    {
    public:
        void pushDestroyEntity(Entity::ID id)
        {
            std::lock_guard lock(_mutex);
            _destroyEntity.push_back({ id });
        }

        void pushDestroyComponent(Entity::ID entt, size_t family)
        {
            std::lock_guard lock(_mutex);
            _destroyComponent.push_back({entt,family });
        }

        void swap(DeferredCommands& target) 
        {
            std::lock_guard lock(_mutex);
            _destroyEntity.swap(target._destroyEntity);
            _destroyComponent.swap(target._destroyComponent);
        }

        const auto& destroyEntityBuf() const noexcept { return _destroyEntity; }

        const auto& destroyComponentBuf() const noexcept { return _destroyComponent; }
        void clear() { _destroyEntity.clear(); _destroyComponent.clear(); }

    private:
        std::vector<DestroyEntityCommand>  _destroyEntity;
        std::vector<DestroyComponentCommand> _destroyComponent;
        std::mutex _mutex;
    };
}


#endif

