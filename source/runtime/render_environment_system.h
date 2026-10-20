#pragma once
#include "core/system_base.h"
#include "core/services.h"
#include "core/time.h"
#include "dx12_graphic/primitives.h"
#include "dx12_graphic/renderer.h"
#include "core/registration.h"

namespace csyren::render
{
    // Registers the engine's procedural render resources. Scene lighting is
    // gathered by LightCollectSystem (dx12_graphic/lighting).
    class RenderEnvironmentSystem : public core::System
    {
    public:
        core::TaskDescription describe() const override {
            core::TaskDescription d; d.phase = "acquire"; d.before = {"FrameAcquireSystem"};
            d.affinity = core::TaskAffinity::MainThread;
            d.readResource<core::Time>(); return d;
        }
        void execute() override {}
        void init() override
        {
            // Register factories before the startup scene resolves its resources.
            if (!Primitives::registerFabricsAll())
                throw std::runtime_error("Failed to register primitive resource factories");
        }
    };
    CS_SYSTEM(RenderEnvironmentSystem);
}
