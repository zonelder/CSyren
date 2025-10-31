#pragma once
#include "core/entity.h"
#include "math/math.h"

namespace csyren::physics
{
	struct SpringJoin
	{
		core::Entity::ID connectedEntity;
		math::Vector3 anchor;
		math::Vector3 connectedAnchor;
		float spring{ 10.0f };
		float damper{ 0.02f };
		float minDistance{ 0.0f };
		float maxDistance{ 1.0f };
		float tolerance{ 0.025f };

	};
}
