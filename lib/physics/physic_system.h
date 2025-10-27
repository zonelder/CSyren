#pragma once

#include "core/context.h"

namespace csyren::physics
{
    class PhysicsEngine
    {
    public:
        PhysicsEngine();
        ~PhysicsEngine();

        void initialize(core::events::SystemEvent& event);
        void shutdown(core::events::SystemEvent& event);

        void update(core::events::SystemEvent& event, core::Scene& scene);

    private:

        class Impl;
        Impl* _pImpl;
    };

}

