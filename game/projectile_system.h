#pragma once
// Explodes projectiles on contact or timeout: radial damage with falloff,
// impulses on dynamic bodies. The trail puffs and the explosion are the
// projectile's prefabs (Projectile::trail / ::explosion), created with Spawn.
#include <algorithm>
#include <cmath>
#include "core/typed_system.h"
#include "core/spawn.h"
#include "core/transform.h"
#include "core/transform_hierarchy.h"
#include "core/time.h"
#include "math/math.h"
#include "physics/physic_system.h"
#include "physics/rigid_body.h"

#include "game_components.h"
#include "game_physics.h"
#include "weapon_system.h"

CS_STRICT_SYSTEMS_BEGIN
namespace csyren::game
{
	class ProjectileSystem : public core::TypedSystem<ProjectileSystem>
	{
	public:
		static constexpr auto phase = "simulation";
		static constexpr auto after = "WeaponSystem";

		using Projectiles = core::Update<Projectile, const core::Transform>;
		// Explosion victims: damage, hit marker of the shooter, impulse on dynamic bodies.
		using Targets = core::Update<Health, WeaponInventory, const physics::RigidBody>;

		void execute(Projectiles projectiles, Targets targets, core::Spawn spawn, core::Delete remove,
			core::Res<physics::PhysicsEngine> engine, core::Res<const core::Time> time)
		{
			using namespace math;
			const float dt = std::clamp(time->deltaTime(), 0.0f, 0.05f);
			const auto& contacts = engine->contacts();

			for (auto [ent, projectile, tr] : projectiles)
			{
				if (projectile.exploded) continue;
				projectile.lifetime -= dt;
				projectile.armTime -= dt;
				bool explode = projectile.lifetime <= 0.0f;
				Vector3 point = core::worldMatrix(projectiles, ent).translation();
				projectile.trailTimer -= dt;
				if (projectile.trailTimer <= 0.0f)
				{
					projectile.trailTimer = 0.035f;
					spawn(projectile.trail, { point });
				}
				if (!explode && projectile.armTime <= 0.0f)
				{
					for (const auto& contact : contacts)
					{
						if (!contact.involves(ent)) continue;
						const auto other = contact.other(ent);
						if (other == projectile.owner) continue;
						explode = true; point = contact.point; break;
					}
				}
				if (!explode) continue;
				projectile.exploded = true;
				detonate(projectiles, targets, spawn, *engine, ent, projectile, point);
				remove.destroy(ent);
			}
		}

	private:
		void detonate(const Projectiles& projectiles, const Targets& targets, const core::Spawn& spawn, physics::PhysicsEngine& engine,
			core::Entity::ID self, const Projectile& projectile, const math::Vector3& center)
		{
			using namespace math;
			const physics::QueryFilter filter = layers::filter(projectile.hitLayers);
			bool hitSomething = false;
			for (auto target : engine.overlapSphere(center, projectile.explosionRadius, filter))
			{
				if (target == self) continue;
				const Vector3 targetPos = core::worldMatrix(projectiles, target).translation();
				Vector3 away = targetPos - center;
				const float distance = away.magnitude();
				const float falloff = 1.0f - std::clamp(distance / (std::max)(projectile.explosionRadius, 0.01f), 0.0f, 1.0f);
				if (auto health = targets.get<Health>(target); health && !health->dead)
				{
					WeaponSystem::damage(*health, projectile.damage * (0.35f + 0.65f * falloff));
					hitSomething = true;
				}
				if (auto body = targets.get<physics::RigidBody>(target); body && body->type == physics::BodyType::Dynamic)
				{
					away = distance > 1e-3f ? away / distance : Vector3::up;
					engine.addImpulse(target, (away + Vector3::up * 0.5f).normalized() * (projectile.impulse * (0.3f + 0.7f * falloff)));
				}
			}
			if (hitSomething && projectile.owner != core::Entity::invalidID)
				if (auto inventory = targets.get<WeaponInventory>(projectile.owner)) inventory->hitMarker = 0.15f;

			core::SpawnPose pose;
			pose.position = center;
			const float size = projectile.explosionRadius * 2.0f;
			pose.scale = Vector3(size, size, size);
			spawn(projectile.explosion, pose);
		}
	};

	REGISTER_SYSTEM(ProjectileSystem)
}
CS_STRICT_SYSTEMS_END
