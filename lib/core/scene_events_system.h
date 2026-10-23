#pragma once
#include "typed_system.h"
#include "registration.h"
CS_STRICT_SYSTEMS_BEGIN
namespace csyren::core {
// Dispatches the frame's scene events. Exclusive: the handlers run arbitrary
// code on types known only at run time.
class SceneEventsSystem final : public TypedSystem<SceneEventsSystem> {
public:
    static constexpr auto phase = "events";
    static constexpr TaskAffinity affinity = TaskAffinity::MainThread;
    void execute(Exclusive world) { world.bus().dispatch(); }
};
CS_SYSTEM(SceneEventsSystem);
}
CS_STRICT_SYSTEMS_END
