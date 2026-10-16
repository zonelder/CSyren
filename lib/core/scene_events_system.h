#pragma once
#include "system_base.h"
#include "scene.h"
namespace csyren::core {
class SceneEventsSystem final : public System {
public:
    TaskDescription describe() const override {
        TaskDescription d; d.phase = "events"; d.affinity = TaskAffinity::MainThread;
        d.exclusiveScene = true; d.dynamicResources = true; d.structuralChanges = true;
        d.writeResource<EventBus2>(); return d;
    }
    void execute() override { Services::get<EventBus2>()->commit_batch(); }
};
REGISTER_SYSTEM(SceneEventsSystem)
}
