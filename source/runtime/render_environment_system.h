#pragma once
#include <stdexcept>
#include "core/system_base.h"
#include "dx12_graphic/primitives.h"
#include "core/registration.h"

namespace csyren::render
{
    // Registers the engine's procedural render resources (init only). Scene
    // lighting is gathered by LightCollectSystem (dx12_graphic/lighting).
    class RenderEnvironmentSystem : public core::OnceSystem
    {
    public:
        void init() override
        {
            // Register factories before the startup scene resolves its resources.
            if (!Primitives::registerFabricsAll())
                throw std::runtime_error("Failed to register primitive resource factories");
        }
    };
    CS_SYSTEM(RenderEnvironmentSystem);
}
