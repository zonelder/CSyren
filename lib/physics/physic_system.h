#pragma once

#include "core/context.h"
#include "core/entity.h"
#include "math/math.h"

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

        void addForce(core::Entity::ID, const math::Vector3& force);

    private:

        class Impl;
        Impl* _pImpl;
    };

}

