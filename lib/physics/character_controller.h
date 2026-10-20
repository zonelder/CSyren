#pragma once
#include <string>
#include "math/math.h"
#include "physics_layers.h"

namespace csyren::physics
{
	// Kinematic capsule character driven by a desired velocity.
	//
	// Input (set by gameplay every frame):
	//   velocity  - desired world velocity for the next physics steps.
	//   height    - desired capsule height (feet to top); changed when crouching.
	// Output (written by PhysicsSystem after stepping):
	//   velocity        - velocity after collision response (sliding along walls/floor).
	//   grounded        - standing on walkable ground.
	//   steepGround     - touching ground that is too steep to walk on.
	//   groundNormal    - normal of the supporting surface.
	//   groundVelocity  - velocity of the supporting body (moving platforms).
	//   currentHeight   - height the capsule actually has (shrinking always works,
	//                     growing waits until there is room).
	//   wallContact     - touching a (near) vertical surface.
	//   wallNormal      - normal of that surface, pointing away from it.
	//
	// The entity Transform position is the feet position.
	struct CharacterController
	{
		float radius = 0.4f;
		float height = 1.8f;
		float maxSlopeDegrees = 50.0f;
		float stepHeight = 0.4f;
		float stickToFloor = 0.5f;
		float mass = 70.0f;
		// Collision layer (PhysicsLayers): decides which bodies the capsule collides with.
		CollisionLayer layer;
		math::Vector3 velocity = math::Vector3::zero;

		bool grounded = false;
		bool steepGround = false;
		math::Vector3 groundNormal{ 0.0f, 1.0f, 0.0f };
		math::Vector3 groundVelocity = math::Vector3::zero;
		float currentHeight = 1.8f;
		bool wallContact = false;
		math::Vector3 wallNormal = math::Vector3::zero;

		static void describe();
	};
}
