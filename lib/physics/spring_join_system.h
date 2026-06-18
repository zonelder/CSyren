#pragma once
#include "core/system_base.h"
#include "core/scene.h"
#include "core/transform.h"
#include "core/services.h"

#include "rigid_body.h"
#include "spring_join.h"
#include "physic_system.h"

namespace csyren::physics
{
	class SpringJoinSystem : public core::System
	{
	public:
		void update() override
		{
			using ctx = core::Services;
			auto physicEngine = ctx::get<PhysicsEngine>();
			auto scene = ctx::get<core::Scene>();
			auto view = scene->view<core::components::Transform,RigidBody, SpringJoin>();
			for (auto [ent,trA, rb, join] : view )
			{
				auto pTrB = scene->getComponent< core::components::Transform>(join.connectedEntity);
				if (!pTrB) continue;
				auto& trB = *pTrB;
				auto posA = trA.position + join.anchor;
				auto posB = trB.position + join.connectedAnchor;

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

					physicEngine->addForce(ent, n * totalForce);
				}
			}
		}
	};

	REGISTER_SYSTEM(SpringJoinSystem)
}
