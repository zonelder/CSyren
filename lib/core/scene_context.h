#pragma once
#include "scene.h"
#include <atomic>
#include <memory>

namespace csyren::core
{
    using SceneInstanceId = uint64_t;
    struct SceneContext
    {
        Scene& scene;
        EventBus& bus;
        SceneInstanceId instance;
    };
    // The bus is destroyed after the scene and its component pools.
    class SceneInstance
    {
    public:
        SceneInstance() : _bus(std::make_unique<EventBus>()),
            _scene(std::make_unique<Scene>(_bus.get()))
        { _scene->init(); }
        SceneInstance(const SceneInstance&) = delete;
        SceneInstance& operator=(const SceneInstance&) = delete;
        SceneContext context() const { return { *_scene, *_bus, _scene->identity() }; }
        Scene& scene() const { return *_scene; }
        void settle() { _scene->flush(); _bus->dispatch(); }
    private:
        std::unique_ptr<EventBus> _bus;
        std::unique_ptr<Scene> _scene;
    };
}
