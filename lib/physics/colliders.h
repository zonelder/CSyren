#pragma once
#include "math/math.h"
#include "core/registration.h"


namespace csyren::physics
{
	struct BoxCollider
	{
		math::Vector3 size{ 1,1,1 };
		math::Vector3 offset{ 0,0,0 };
		math::Quaternion rotation = math::Quaternion::identity;
		bool isTrigger{ false };

		static void describe();
	};
	CS_COMPONENT(BoxCollider);

	struct SphereCollider
	{
		float radius{ 0.5f };
		math::Vector3 center{ 0,0,0 };
		bool isTrigger{ false };

		static void describe();
	};
	CS_COMPONENT(SphereCollider);

	struct CapsuleCollider
	{
		float radius{ 0.5f };
		float height{ 1.0f };
		math::Vector3 offset{ 0,0,0 };
		math::Quaternion rotation = math::Quaternion::identity;
		bool isTrigger{ false };

		static void describe();
	};
	CS_COMPONENT(CapsuleCollider);
}
