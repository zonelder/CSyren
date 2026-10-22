#pragma once
#include <algorithm>
#include "core/typed_system.h"
#include "core/transform.h"
#include "core/transform_hierarchy.h"

#include "rigid_body.h"
#include "spring_join.h"
#include "physic_system.h"
#include "core/registration.h"

CS_STRICT_SYSTEMS_BEGIN
namespace csyren::physics
{
	class SpringJoinSystem : public core::TypedSystem<SpringJoinSystem>
	{
	public:
		static constexpr auto phase = "simulation";

		// The connected entity is only required to have a Transform (get<Transform>).
		using Joints = core::Select<core::Transform, RigidBody, SpringJoin>;

		void execute(Joints joints, core::Res<PhysicsEngine> physics)
		{
			auto& physicEngine = *physics; // a missing engine fails every frame, as before
			for (auto [ent, trA, rb, join] : joints)
			{
				if (!joints.get<core::Transform>(join.connectedEntity)) continue;
				auto posA = core::worldMatrix(joints, ent).multiplyPoint3x4(join.anchor);
				auto posB = core::worldMatrix(joints, join.connectedEntity).multiplyPoint3x4(join.connectedAnchor);

				auto delta = posB - posA;
				float dist = delta.magnitude();

				if (dist < 1e-5f) continue;

				auto n = delta / dist;

				if (dist < join.minDistance - join.tolerance || dist > join.maxDistance + join.tolerance)
				{
					auto velocity = rb.linearVelocity;
					float vAlongJoin = velocity.dot(n);
					float clampedDist = std::clamp(dist, join.minDistance, join.maxDistance);
					float displacement = dist - clampedDist;
					float Fspring = join.spring * displacement;
					float Fdamp = join.damper * vAlongJoin;
					float totalForce = Fspring + Fdamp;

					physicEngine.addForce(ent, n * totalForce);
				}
			}
		}
	};

	CS_SYSTEM(SpringJoinSystem);
}
CS_STRICT_SYSTEMS_END
