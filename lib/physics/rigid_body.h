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

		math::Vector3 linearVelocity = math::Vector3::zero;
		math::Vector3 angularVelocity = math::Vector3::zero;
	};
}
