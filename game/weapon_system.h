#pragma once
// Weapon handling for the pilot: hitscan rifle (raycast + impact marker) and a
// physical grenade launcher (dynamic projectile entity). Spawns entities through
// the scene transaction (structuralChanges), so new bodies appear next frame.
#include <algorithm>
#include <cmath>
#include <random>
#include "core/system_base.h"
#include "core/services.h"
#include "core/scene.h"
#include "core/transform.h"
#include "core/transform_hierarchy.h"
#include "core/camera.h"
#include "core/time.h"
#include "math/math.h"
#include "physics/physic_system.h"
#include "physics/rigid_body.h"
#include "physics/colliders.h"
#include "physics/character_controller.h"
#include "dx12_graphic/primitives.h"
#include "../mesh_filter.h"
#include "../animation.h"
#include "dx12_graphic/resource_manager.h"
#include "dx12_graphic/animation_clip.h"

#include "game_components.h"
#include "game_physics.h"
#include "game_materials.h"
#include "player_input.h"

namespace csyren::game
{
	class WeaponSystem : public core::System
	{
	public:
		core::TaskDescription describe() const override
		{
			core::TaskDescription d; d.phase = "simulation"; d.after = { "CameraRigSystem" };
			d.structuralChanges = true;
			d.read<PlayerTag>().write<PilotState>().write<WeaponInventory>().write<Health>().read<core::Camera>().read<physics::CharacterController>();
			d.read<ViewModelSlot>().write<Animator>();
			d.write<core::Transform>().write<physics::RigidBody>().write<physics::SphereCollider>().write<physics::BoxCollider>()
				.write<render::MeshFilter>().write<render::MeshRenderer>().write<Projectile>().write<Lifetime>();
			d.readResource<PlayerInput>().readResource<core::Time>().readResource<physics::PhysicsEngine>().readResource<core::SceneEntities>();
			d.readResource<ViewModelState>();
			d.after = { "ViewModelSystem" };
			return d;
		}

		void execute() override
		{
			using namespace math;
			const auto& in = *core::Services::get<const PlayerInput>();
			const auto& viewModel = *core::Services::get<const ViewModelState>();
			const auto* time = core::Services::get<const core::Time>();
			const auto* engine = core::Services::get<const physics::PhysicsEngine>();
			const float dt = std::clamp(time->deltaTime(), 0.0f, 0.05f);
			auto& scene = this->scene();

			for (auto [ent, tag, state, inventory] : scene.view<const PlayerTag, PilotState, WeaponInventory>())
			{
				if (inventory.ammoPrimary < 0) inventory.ammoPrimary = inventory.primary.magazine;
				if (inventory.ammoSecondary < 0) inventory.ammoSecondary = inventory.secondary.magazine;
				inventory.cooldown = (std::max)(0.0f, inventory.cooldown - dt);
				inventory.hitMarker = (std::max)(0.0f, inventory.hitMarker - dt);
				inventory.viewKick = (std::max)(0.0f, inventory.viewKick - dt * 6.0f);

				if (in.swapPressed) switchWeapon(inventory, inventory.active == 0 ? 1 : 0);
				if (in.selectWeapon >= 0) switchWeapon(inventory, in.selectWeapon);

				const WeaponDef& weapon = inventory.active == 0 ? inventory.primary : inventory.secondary;
				int& ammo = inventory.active == 0 ? inventory.ammoPrimary : inventory.ammoSecondary;

				if (inventory.reloading)
				{
					inventory.reloadTimer -= dt;
					if (inventory.reloadTimer <= 0.0f) { inventory.reloading = false; ammo = weapon.magazine; }
					continue;
				}
				if ((in.reloadPressed && ammo < weapon.magazine) || (ammo <= 0 && (in.firePressed || in.fireHeld)))
				{
					inventory.reloading = true; inventory.reloadTimer = weapon.reloadTime;
					startReloadAnimation(scene, inventory.active, viewModel);
					continue;
				}
				if (viewModel.switching) continue; // no firing while the weapon is raised

				const bool wantsFire = weapon.automatic ? in.fireHeld : in.firePressed;
				if (!wantsFire || inventory.cooldown > 0.0f || ammo <= 0 || !in.captured) continue;
				if (state.cameraEntity == core::Entity::invalidID) continue;

				inventory.cooldown = 60.0f / (std::max)(weapon.rpm, 1.0f);
				--ammo;

				const auto cameraWorld = core::worldMatrix(scene, state.cameraEntity);
				const auto cameraRotation = core::worldRotation(scene, state.cameraEntity);
				const Vector3 origin = cameraWorld.translation();
				Vector3 direction = (cameraRotation * Vector3::forward).normalized();

				auto cc = scene.getComponent<const physics::CharacterController>(ent);
				const bool unstable = state.move != PilotMove::Ground || (cc && cc->velocity.sqrMagnitude() > 1.0f && state.move == PilotMove::Slide);
				const float spread = unstable ? weapon.spreadMoving : weapon.spread;
				direction = scatter(direction, cameraRotation, spread);

				state.recoilPitch += weapon.recoilPitch;
				state.recoilYaw += symmetric(weapon.recoilYaw);
				inventory.viewKick = (std::min)(inventory.viewKick + weapon.recoilPitch * 1.5f, 3.0f);

				const Vector3 muzzle = viewModel.hasMuzzle ? viewModel.muzzlePosition : origin + direction * 0.7f;
				spawnMarker(scene, muzzle + direction * 0.05f, Vector3(0.16f, 0.16f, 0.28f), _flashMaterial, _cube, 0.05f, cameraRotation);
				if (weapon.kind == WeaponKind::Hitscan) fireHitscan(scene, *engine, ent, inventory, weapon, origin, direction);
				else fireProjectile(scene, ent, weapon, muzzle, direction, cc ? cc->velocity : Vector3::zero);
			}
		}

	protected:
		void onAttach() override
		{
			_impactMaterial = materials::get(materials::Impact);
			_projectileMaterial = materials::get(materials::Projectile);
			_flashMaterial = materials::get(materials::Explosion);
			_cube = render::Primitives::getCube();
			_sphere = render::Primitives::getSphere();
			_defaultMaterial = render::Primitives::getDefaultMaterial();
			// Resolve reload clips once (outside the task graph) and let the clip
			// length define the reload time.
			auto& resources = *core::Services::get<render::ResourceManager>();
			for (auto [ent, inventory] : scene().view<WeaponInventory>())
			{
				WeaponDef* defs[2] = { &inventory.primary, &inventory.secondary };
				for (int slot = 0; slot < 2; ++slot)
				{
					_reloadClips[slot] = {};
					if (defs[slot]->reloadClip.empty()) continue;
					_reloadClips[slot] = resources.get<render::AnimationClip>(defs[slot]->reloadClip);
					if (const auto* clip = resources.getAnimationClip(_reloadClips[slot]); clip && clip->duration() > 0.05f)
						defs[slot]->reloadTime = clip->duration();
				}
				break;
			}
		}

		void startReloadAnimation(core::Scene& scene, int slot, const ViewModelState& viewModel)
		{
			if (slot < 0 || slot > 1 || !_reloadClips[slot] || viewModel.activeSlotEntity == core::Entity::invalidID) return;
			auto animator = scene.getComponent<Animator>(viewModel.activeSlotEntity);
			if (!animator) return;
			playAnimation(*animator, _reloadClips[slot], false);
		}

	private:
		static void switchWeapon(WeaponInventory& inventory, int slot)
		{
			if (slot == inventory.active) return;
			inventory.active = std::clamp(slot, 0, 1);
			inventory.reloading = false; inventory.cooldown = (std::max)(inventory.cooldown, 0.35f);
		}
		float symmetric(float amplitude)
		{
			std::uniform_real_distribution<float> dist(-amplitude, amplitude);
			return dist(_random);
		}
		math::Vector3 scatter(const math::Vector3& direction, const math::Quaternion& rotation, float spreadDegrees)
		{
			using namespace math;
			if (spreadDegrees <= 0.0f) return direction;
			std::uniform_real_distribution<float> angle(0.0f, DirectX::XM_2PI);
			std::uniform_real_distribution<float> radius(0.0f, 1.0f);
			const float a = angle(_random);
			const float r = std::sqrt(radius(_random)) * std::tan(DirectX::XMConvertToRadians(spreadDegrees));
			const Vector3 right = rotation * Vector3::right;
			const Vector3 up = rotation * Vector3::up;
			return (direction + right * (std::cos(a) * r) + up * (std::sin(a) * r)).normalized();
		}

		void fireHitscan(core::Scene& scene, const physics::PhysicsEngine& engine, core::Entity::ID shooter, WeaponInventory& inventory,
			const WeaponDef& weapon, const math::Vector3& origin, const math::Vector3& direction)
		{
			using namespace math;
			const physics::QueryFilter filter = layers::filter(weapon.hitLayers, shooter);
			const auto hit = engine.raycast(origin, direction, weapon.range, filter);
			if (!hit.hit) return;
			if (auto health = scene.getComponent<Health>(hit.entity); health && !health->dead)
			{
				damage(*health, weapon.damage);
				inventory.hitMarker = 0.12f;
			}
			spawnMarker(scene, hit.point + hit.normal * 0.02f, Vector3(0.12f, 0.12f, 0.12f), _impactMaterial, _cube, 2.5f);
		}

		void fireProjectile(core::Scene& scene, core::Entity::ID shooter, const WeaponDef& weapon,
			const math::Vector3& origin, const math::Vector3& direction, const math::Vector3& inheritVelocity)
		{
			using namespace math;
			using namespace physics;
			const Vector3 spawnPos = origin + direction * 0.15f;
			auto entity = scene.createEntity("projectile");
			auto tr = scene.addComponent<core::Transform>(entity);
			tr->position = spawnPos;
			const float size = weapon.projectileRadius * 2.0f * 2.4f; // visual ball bigger than the collider
			tr->scale = Vector3(size, size, size);
			auto collider = scene.addComponent<SphereCollider>(entity);
			collider->radius = weapon.projectileRadius;
			RigidBody body;
			body.type = BodyType::Dynamic;
			body.mass = 0.5f;
			body.useGravity = weapon.projectileGravity;
			body.friction = 0.8f; body.restitution = 0.1f;
			body.layer = layers::of(Layer::Projectile);
			body.continuousCollision = true; // fast + small: no tunnelling through walls
			body.linearVelocity = direction * weapon.projectileSpeed + inheritVelocity * 0.35f;
			scene.addComponent<RigidBody>(entity, body);
			auto filter = scene.addComponent<render::MeshFilter>(entity); filter->mesh = _sphere;
			auto renderer = scene.addComponent<render::MeshRenderer>(entity); renderer->material = _projectileMaterial;
			Projectile projectile;
			projectile.damage = weapon.damage; projectile.explosionRadius = weapon.explosionRadius;
			projectile.impulse = weapon.explosionImpulse; projectile.lifetime = weapon.projectileLifetime;
			projectile.owner = shooter;
			projectile.hitLayers = weapon.hitLayers;
			scene.addComponent<Projectile>(entity, projectile);
		}

		void spawnMarker(core::Scene& scene, const math::Vector3& position, const math::Vector3& scale,
			render::MaterialHandle material, render::MeshHandle mesh, float lifetime, const math::Quaternion& rotation = math::Quaternion::identity)
		{
			auto entity = scene.createEntity("impact");
			auto tr = scene.addComponent<core::Transform>(entity);
			tr->position = position; tr->scale = scale; tr->rotation = rotation;
			auto filter = scene.addComponent<render::MeshFilter>(entity); filter->mesh = mesh;
			auto renderer = scene.addComponent<render::MeshRenderer>(entity); renderer->material = material ? material : _defaultMaterial;
			auto life = scene.addComponent<Lifetime>(entity); life->remaining = lifetime;
		}

	public:
		static void damage(Health& health, float amount)
		{
			if (health.dead) return;
			health.current -= amount;
			health.flash = 0.12f;
			if (health.current <= 0.0f) { health.current = 0.0f; health.dead = true; health.respawnTimer = health.respawnTime; }
		}

	private:
		std::mt19937 _random{ 1337u };
		render::MaterialHandle _impactMaterial, _projectileMaterial, _defaultMaterial, _flashMaterial;
		render::MeshHandle _cube, _sphere;
		render::AnimationClipHandle _reloadClips[2];
	};

	REGISTER_SYSTEM(WeaponSystem)
}
