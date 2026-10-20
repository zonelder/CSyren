#pragma once

#include "core/entity.h"
#include "core/system_base.h"

#include "math/math.h"
#include "physics_query.h"
#include "physics_layers.h"

#include <vector>
#include "core/registration.h"

namespace csyren::physics
{
    // Steps the simulation with a fixed time step (core::Time::fixedDeltaTime)
    // and writes interpolated poses to Transform, so render motion stays smooth
    // at any frame rate. Kinematic CharacterController components are advanced
    // in the same fixed steps, before the rigid bodies.
    class PhysicsSystem : public core::System
    {
    public:
        core::TaskDescription describe() const override;

        PhysicsSystem() = default;
        void init() override;
        void execute() override;
        void shutdown() override;
    protected:
        void onAttach() override;
        void onDetach() noexcept override;

    };

    class PhysicsEngine
    {
        friend PhysicsSystem;
    public:
        PhysicsEngine();
        ~PhysicsEngine();

        // Continuous force, applied on every fixed step of the next frame.
        void addForce(core::Entity::ID, const math::Vector3& force);
        // Instant change of momentum, applied once before the next step.
        void addImpulse(core::Entity::ID, const math::Vector3& impulse);
        void setGravity(const math::Vector3& g);
        math::Vector3 gravity() const;

        // Scene queries. Safe to call from any system that declares read access
        // to PhysicsEngine; results are in world space.
        RaycastHit raycast(const math::Vector3& origin, const math::Vector3& direction, float maxDistance, const QueryFilter& filter = {}) const;
        RaycastHit sphereCast(const math::Vector3& origin, float radius, const math::Vector3& direction, float maxDistance, const QueryFilter& filter = {}) const;
        std::vector<core::Entity::ID> overlapSphere(const math::Vector3& center, float radius, const QueryFilter& filter = {}) const;

        // Contacts that started during the last frame (all fixed steps).
        const std::vector<ContactInfo>& contacts() const;

        // Fraction of the fixed step not yet simulated, for custom interpolation.
        float interpolationAlpha() const;

    private:
        class Impl;
        Impl* _pImpl;
    };

    CS_SYSTEM(PhysicsSystem);

}
