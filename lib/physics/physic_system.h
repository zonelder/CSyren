#pragma once

#include "core/entity.h"
#include "core/system_base.h"

#include "math/math.h"

namespace csyren::physics
{
    class PhysicsSystem : public core::System
    {
    public:
        PhysicsSystem() = default;
        void init() override;
        void update() override;
        void shutdown() override;

    };
    class PhysicsEngine
    {
        friend PhysicsSystem;
    public:
        PhysicsEngine();
        ~PhysicsEngine();

        void addForce(core::Entity::ID, const math::Vector3& force);
        void setGravity(const math::Vector3& g);

    private:
        class Impl;
        Impl* _pImpl;
    };

}

