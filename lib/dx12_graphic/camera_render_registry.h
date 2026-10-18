#pragma once
#include "camera_render_target.h"
#include "camera_render_view.h"
#include "core/transform.h"
#include "core/camera.h"
#include "core/scene.h"
#include <unordered_map>

namespace csyren::render::details
{
    // Per attached scene. ComponentRef tracks pool relocation and component
    // generations, so delayed removal cannot erase a replacement camera.
    class CameraRenderRegistry
    {
    public:
        ~CameraRenderRegistry() { detach(); }
        void attach(core::Scene& scene)
        {
            detach(); _bus = &scene.eventBus();
            _created = _bus->subscribe<core::ComponentCreateEvent<core::Camera>>(
                [this](auto& event) { add(event.comp); });
            _destroyed = _bus->subscribe<core::ComponentDestroyEvent<core::Camera>>(
                [this](auto& event) {
                    auto it = _entries.find(event.comp.id());
                    if (it != _entries.end() && it->second.camera == event.comp) _entries.erase(it);
                });
            for (auto [id,camera] : scene.view<const core::Camera>()) add(scene.getComponent<core::Camera>(id));
        }
        void detach() noexcept
        {
            if (_bus) { _bus->unsubscribe(_created); _bus->unsubscribe(_destroyed); }
            _bus = nullptr; _created = {}; _destroyed = {}; _entries.clear();
        }
        std::shared_ptr<CameraRenderTarget> target(const core::Camera& camera) const
        {
            for (const auto& [id,entry] : _entries)
                if (entry.camera.get() == &camera) return entry.view->target;
            return {};
        }
        template<class Callback> void each(core::Scene& scene,Callback callback)
        {
            for (auto& [id,entry] : _entries) {
                auto transform = scene.getComponent<const core::Transform>(id);
                if (entry.camera && transform) callback(id,*entry.camera,*transform,entry.view);
            }
        }
        bool request(core::Entity::ID id)
        {
            auto it = _entries.find(id); if (it == _entries.end()) return false;
            it->second.view->schedule.request(); return true;
        }
        size_t size() const { return _entries.size(); }
    private:
        void add(core::ComponentRef<core::Camera> camera)
        {
            if (!camera) return;
            auto it = _entries.find(camera.id());
            if (it != _entries.end() && it->second.camera == camera) return;
            _entries.insert_or_assign(camera.id(),Entry{camera,std::make_shared<CameraRenderView>()});
        }
        struct Entry { core::ComponentRef<core::Camera> camera; std::shared_ptr<CameraRenderView> view; };
        std::unordered_map<core::Entity::ID,Entry> _entries;
        core::EventBus2* _bus = nullptr;
        core::SubscriberToken _created,_destroyed;
    };
}
