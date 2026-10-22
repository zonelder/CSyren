#pragma once

#include "core/entity.h"
#include "core/typed_system.h"
#include "core/transform.h"
#include "core/time.h"

#include "math/math.h"
#include "physics_query.h"
#include "physics_layers.h"
#include "rigid_body.h"
#include "colliders.h"
#include "character_controller.h"

#include <vector>
#include "core/registration.h"

CS_STRICT_SYSTEMS_BEGIN
namespace csyren::physics
{
    class PhysicsEngine;

    // Steps the simulation with a fixed time step (core::Time::fixedDeltaTime)
    // and writes interpolated poses to Transform, so render motion stays smooth
    // at any frame rate. Kinematic CharacterController components are advanced
    // in the same fixed steps, before the rigid bodies. The Jolt world is built
    // from the scene when the system attaches; later scene changes (queued by
    // the event handlers) are applied by PhysicsSyncSystem, and whatever is still
    // queued is applied here before the step (changes dispatched between frames,
    // or no PhysicsSyncSystem in the schedule).
    class PhysicsSystem : public core::TypedSystem<PhysicsSystem>
    {
    public:
        static constexpr auto phase = "simulation";

        using Bodies = core::Update<RigidBody, core::Transform>;
        // Shapes, for bodies rebuilt before the step.
        using Colliders = core::Select<BoxCollider, SphereCollider, CapsuleCollider>;
        // Every character (its Transform is reached through Bodies).
        using Characters = core::Update<CharacterController>;

        void execute(Bodies bodies, Colliders colliders, Characters characters, core::Res<PhysicsEngine> engine,
            core::Res<const core::Time> time, core::Res<const PhysicsLayers> layers);
    protected:
        void onAttach() override;
        void onDetach() noexcept override;
    };

    // Applies the scene's physics changes to the Jolt world: bodies and characters
    // whose components were created, patched or destroyed (the engine queues them
    // while the scene events are dispatched). Runs right after SceneEventsSystem,
    // so new bodies are visible to physics queries for the whole next frame, also
    // to systems that run before PhysicsSystem. Optional (PhysicsSystem applies
    // what is left before stepping); SceneEventsSystem must be in the schedule.
    class PhysicsSyncSystem : public core::TypedSystem<PhysicsSyncSystem>
    {
    public:
        static constexpr auto phase = "events";
        static constexpr auto after = "SceneEventsSystem";

        // Read for (re)building bodies: shapes, body settings and world poses.
        using Sources = core::Select<core::Transform, RigidBody, BoxCollider, SphereCollider, CapsuleCollider>;
        // Characters (the rebuild records the shape height).
        using Characters = core::Update<CharacterController>;

        void execute(Sources sources, Characters characters, core::Res<PhysicsEngine> engine, core::Res<const PhysicsLayers> layers);
    };

    class PhysicsEngine
    {
        friend PhysicsSystem;
        friend PhysicsSyncSystem;
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
    CS_SYSTEM(PhysicsSyncSystem);

}
CS_STRICT_SYSTEMS_END
