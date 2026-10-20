#pragma once
#include <cstdint>
#include <string>
#include "math/math.h"
#include "core/reflected_enum.h"
#include "physics_layers.h"
#include "core/registration.h"

namespace csyren::physics
{
	CS_ENUM(BodyType, int, Static, Kinematic, Dynamic)

	struct RigidBody
	{
		BodyType type = BodyType::Static;
		float mass = 1.0f;
		bool useGravity = true;
		float friction = 0.6f;
		float restitution = 1.0f;
		math::Vector3 linearVelocity = math::Vector3::zero;
		math::Vector3 angularVelocity = math::Vector3::zero;
		// Collision layer (PhysicsLayers); unknown names behave like "Default".
		CollisionLayer layer;
		// Continuous collision (Jolt LinearCast) for fast dynamic bodies (bullets, grenades).
		bool continuousCollision = false;

		static void describe();
	};
	CS_COMPONENT(RigidBody);
}
