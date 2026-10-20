#pragma once
// Explodes projectiles on contact or timeout: radial damage with falloff,
// impulses on dynamic bodies and a short-lived explosion marker.
#include <algorithm>
#include <cmath>
#include "core/system_base.h"
#include "core/services.h"
#include "core/scene.h"
#include "core/transform.h"
#include "core/transform_hierarchy.h"
#include "core/time.h"
#include "math/math.h"
#include "physics/physic_system.h"
#include "physics/rigid_body.h"
#include "dx12_graphic/primitives.h"
#include "../mesh_filter.h"

#include "game_components.h"
#include "game_physics.h"
#include "game_materials.h"
#include "weapon_system.h"

namespace csyren::game
{
	class ProjectileSystem : public core::System
	{
	public:
		core::TaskDescription describe() const override
		{
			core::TaskDescription d; d.phase = "simulation"; d.after = { "WeaponSystem" };
			d.structuralChanges = true;
			d.write<Projectile>().write<Health>().write<WeaponInventory>().read<physics::RigidBody>();
			d.write<core::Transform>().write<render::MeshFilter>().write<render::MeshRenderer>().write<Lifetime>();
			d.readResource<core::Time>().writeResource<physics::PhysicsEngine>().readResource<core::SceneEntities>();
			return d;
		}

		void execute() override
		{
			using namespace math;
			const auto* time = core::Services::get<const core::Time>();
			auto* engine = core::Services::get<physics::PhysicsEngine>();
			const float dt = std::clamp(time->deltaTime(), 0.0f, 0.05f);
			auto& scene = this->scene();
			const auto& contacts = engine->contacts();

			for (auto [ent, projectile, tr] : scene.view<Projectile, const core::Transform>())
			{
				if (projectile.exploded) continue;
				projectile.lifetime -= dt;
				projectile.armTime -= dt;
				bool explode = projectile.lifetime <= 0.0f;
				Vector3 point = core::worldMatrix(scene, ent).translation();
				projectile.trailTimer -= dt;
				if (projectile.trailTimer <= 0.0f)
				{
					projectile.trailTimer = 0.035f;
					auto puff = scene.createEntity("trail");
					auto puffTr = scene.addComponent<core::Transform>(puff);
					puffTr->position = point; puffTr->scale = Vector3(0.11f, 0.11f, 0.11f);
					auto puffMesh = scene.addComponent<render::MeshFilter>(puff); puffMesh->mesh = _sphere;
					auto puffRenderer = scene.addComponent<render::MeshRenderer>(puff); puffRenderer->material = _trailMaterial;
					auto puffLife = scene.addComponent<Lifetime>(puff); puffLife->remaining = 0.35f;
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
				detonate(scene, *engine, ent, projectile, point);
				scene.destroyEntity(ent);
			}
		}

	protected:
		void onAttach() override
		{
			_explosionMaterial = materials::get(materials::Explosion);
			_trailMaterial = materials::get(materials::Projectile);
			_sphere = render::Primitives::getSphere();
		}

	private:
		void detonate(core::Scene& scene, physics::PhysicsEngine& engine, core::Entity::ID self, const Projectile& projectile, const math::Vector3& center)
		{
			using namespace math;
			const physics::QueryFilter filter = layers::filter(projectile.hitLayers);
			bool hitSomething = false;
			for (auto target : engine.overlapSphere(center, projectile.explosionRadius, filter))
			{
				if (target == self) continue;
				const Vector3 targetPos = core::worldMatrix(scene, target).translation();
				Vector3 away = targetPos - center;
				const float distance = away.magnitude();
				const float falloff = 1.0f - std::clamp(distance / (std::max)(projectile.explosionRadius, 0.01f), 0.0f, 1.0f);
				if (auto health = scene.getComponent<Health>(target); health && !health->dead)
				{
					WeaponSystem::damage(*health, projectile.damage * (0.35f + 0.65f * falloff));
					hitSomething = true;
				}
				if (auto body = scene.getComponent<const physics::RigidBody>(target); body && body->type == physics::BodyType::Dynamic)
				{
					away = distance > 1e-3f ? away / distance : Vector3::up;
					engine.addImpulse(target, (away + Vector3::up * 0.5f).normalized() * (projectile.impulse * (0.3f + 0.7f * falloff)));
				}
			}
			if (hitSomething && projectile.owner != core::Entity::invalidID)
				if (auto inventory = scene.getComponent<WeaponInventory>(projectile.owner)) inventory->hitMarker = 0.15f;

			auto marker = scene.createEntity("explosion");
			auto tr = scene.addComponent<core::Transform>(marker);
			tr->position = center;
			const float size = projectile.explosionRadius * 2.0f;
			tr->scale = Vector3(size, size, size);
			auto mesh = scene.addComponent<render::MeshFilter>(marker); mesh->mesh = _sphere;
			auto renderer = scene.addComponent<render::MeshRenderer>(marker); renderer->material = _explosionMaterial;
			auto life = scene.addComponent<Lifetime>(marker); life->remaining = 0.18f;
		}

		render::MaterialHandle _explosionMaterial, _trailMaterial;
		render::MeshHandle _sphere;
	};

	REGISTER_SYSTEM(ProjectileSystem)
}
