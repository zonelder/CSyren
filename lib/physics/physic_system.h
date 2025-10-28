#pragma once

#include "core/context.h"

namespace csyren::physics
{
    class PhysicsEngine
    {
    public:
        PhysicsEngine();
        ~PhysicsEngine();

        void initialize(core::ServiceContext& ctx);
        void shutdown(core::ServiceContext& ctx);

        void update(core::ServiceContext& ctx);

    private:

        class Impl;
        Impl* _pImpl;
    };

}

