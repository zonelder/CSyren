#pragma once
#include <cstdint>


namespace JPH { class BodyID; }


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

		uint32_t internalID = 0xFFFFFFFF;
		bool isNew = true;

	};
}
