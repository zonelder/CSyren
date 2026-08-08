#pragma once
#include <cstdint>
#include "math/math.h"

namespace csyren::physics
{
	enum class BodyType
	{
		Static,
		Kinematic,
		Dynamic,
	};


	struct RigidBody
	{
		BodyType type = BodyType::Static;
		float mass = 1.0f;
		bool useGravity = true;
		float friction = 0.6f;
		float restitution = 1.0f;
		math::Vector3 linearVelocity = math::Vector3::zero;
		math::Vector3 angularVelocity = math::Vector3::zero;

		static void describe();

	};
}
