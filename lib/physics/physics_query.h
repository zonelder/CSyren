#pragma once
// Engine-facing physics query types. Intentionally free of Jolt headers so
// game code built without Jolt defines can use them.
#include <cstdint>
#include <vector>
#include "core/entity.h"
#include "math/math.h"

namespace csyren::physics
{
	// Layer bit masks are built from PhysicsLayers (physics_layers.h): bit i = layer index i.
	struct QueryFilter
	{
		uint32_t layerMask = 0xFFFFFFFFu;
		core::Entity::ID ignore = core::Entity::invalidID;
	};

	struct RaycastHit
	{
		bool hit = false;
		core::Entity::ID entity = core::Entity::invalidID;
		math::Vector3 point;
		math::Vector3 normal{ 0.0f, 1.0f, 0.0f };
		float distance = 0.0f;
		float fraction = 1.0f;
		explicit operator bool() const noexcept { return hit; }
	};

	// A contact reported by the simulation during the last frame.
	struct ContactInfo
	{
		core::Entity::ID a = core::Entity::invalidID;
		core::Entity::ID b = core::Entity::invalidID;
		math::Vector3 point;
		math::Vector3 normal; // points from a towards b
		bool involves(core::Entity::ID id) const noexcept { return a == id || b == id; }
		core::Entity::ID other(core::Entity::ID id) const noexcept { return a == id ? b : a; }
	};
}
