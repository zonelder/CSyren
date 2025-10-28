#pragma once
#include <Jolt/Jolt.h>
#include <Jolt/Physics/Collision/Shape/BoxShape.h>
#include <Jolt/Physics/Collision/Shape/SphereShape.h>
#include <Jolt/Physics/Collision/Shape/CapsuleShape.h>

#include "colliders.h"
#include "math_converter.h"

namespace csyren::physics::details
{
	inline JPH::Ref<JPH::Shape> createBoxShape(const BoxCollider& box)
	{
		return new JPH::BoxShape(details::to_jolt(box.size) * 0.5f);
	}

	inline JPH::Ref<JPH::Shape> createSphereShape(const SphereCollider& sphere)
	{
		return new JPH::SphereShape(sphere.radius);
	}

	inline JPH::Ref<JPH::Shape> createCapsuleShape(const CapsuleCollider& cap)
	{
		return new JPH::CapsuleShape(cap.height * 0.5f, cap.radius);
	}
}
