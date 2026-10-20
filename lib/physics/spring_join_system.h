#pragma once
#include "core/system_base.h"
#include "core/scene.h"
#include "core/transform.h"
#include "core/transform_hierarchy.h"
#include "core/services.h"

#include "rigid_body.h"
#include "spring_join.h"
#include "physic_system.h"
#include "core/registration.h"

namespace csyren::physics
{
	class SpringJoinSystem : public core::System
	{
	public:
        core::TaskDescription describe() const override {
            core::TaskDescription d; d.phase = "simulation";
            d.read<core::Transform>().read<RigidBody>().read<SpringJoin>().writeResource<PhysicsEngine>().readResource<core::SceneEntities>(); return d;
        }

		void execute() override
		{
			using ctx = core::Services;
			auto physicEngine = ctx::get<PhysicsEngine>();
			auto scene = &this->scene();
			auto view = scene->view<const core::Transform,const RigidBody, const SpringJoin>();
			for (auto [ent,trA, rb, join] : view )
			{
				auto pTrB = scene->getComponent<const core::Transform>(join.connectedEntity);
				if (!pTrB) continue;
				auto& trB = *pTrB;
				auto posA = core::worldMatrix(*scene,ent).multiplyPoint3x4(join.anchor);
				auto posB = core::worldMatrix(*scene,join.connectedEntity).multiplyPoint3x4(join.connectedAnchor);

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

	CS_SYSTEM(SpringJoinSystem);
}
