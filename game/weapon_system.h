#pragma once
// Weapon handling for the pilot: hitscan rifle (raycast + impact marker) and a
// physical grenade launcher (dynamic projectile). Muzzle flash, impact marker and
// projectile are prefabs of the weapon (WeaponDef::*Prefab), created with Spawn:
// they join the scene in this system's commit, so ProjectileSystem (ordered
// after it) sees a new grenade in the same frame; physics builds its body at
// the end of the frame.
#include <algorithm>
#include <array>
#include <cmath>
#include <random>
#include "core/typed_system.h"
#include "core/spawn.h"
#include "core/transform.h"
#include "core/transform_hierarchy.h"
#include "core/camera.h"
#include "core/time.h"
#include "math/math.h"
#include "physics/physic_system.h"
#include "physics/rigid_body.h"
#include "physics/colliders.h"
#include "physics/character_controller.h"
#include "animation.h"
#include "dx12_graphic/resource_manager.h"
#include "dx12_graphic/animation_clip.h"

#include "game_components.h"
#include "game_physics.h"
#include "player_input.h"

CS_STRICT_SYSTEMS_BEGIN
namespace csyren::game
{
	class WeaponSystem : public core::TypedSystem<WeaponSystem>
	{
	public:
		static constexpr auto phase = "simulation";
		static constexpr std::array after{ "CameraRigSystem", "ViewModelSystem" };

		using Players = core::Update<PilotState, WeaponInventory>::Where<PlayerTag>;
		// Pilot velocity and the camera's world transform.
		using Bodies = core::Select<physics::CharacterController, core::Transform>;
		// Health of hit targets, Animator of the active view model slot.
		using Targets = core::Update<Health, Animator>;

		void execute(Players players, Bodies bodies, Targets targets, core::Spawn spawn,
			core::Res<const PlayerInput> input, core::Res<const ViewModelState> viewModelState,
			core::Res<const core::Time> time, core::Res<const physics::PhysicsEngine> engine)
		{
			using namespace math;
			const auto& in = *input;
			const auto& viewModel = *viewModelState;
			const float dt = std::clamp(time->deltaTime(), 0.0f, 0.05f);

			for (auto [ent, state, inventory] : players)
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
					startReloadAnimation(targets, inventory.active, viewModel);
					continue;
				}
				if (viewModel.switching) continue; // no firing while the weapon is raised

				const bool wantsFire = weapon.automatic ? in.fireHeld : in.firePressed;
				if (!wantsFire || inventory.cooldown > 0.0f || ammo <= 0 || !in.captured) continue;
				if (state.cameraEntity == core::Entity::invalidID) continue;

				inventory.cooldown = 60.0f / (std::max)(weapon.rpm, 1.0f);
				--ammo;

				const auto cameraWorld = core::worldMatrix(bodies, state.cameraEntity);
				const auto cameraRotation = core::worldRotation(bodies, state.cameraEntity);
				const Vector3 origin = cameraWorld.translation();
				Vector3 direction = (cameraRotation * Vector3::forward).normalized();

				auto cc = bodies.get<physics::CharacterController>(ent);
				const bool unstable = state.move != PilotMove::Ground || (cc && cc->velocity.sqrMagnitude() > 1.0f && state.move == PilotMove::Slide);
				const float spread = unstable ? weapon.spreadMoving : weapon.spread;
				direction = scatter(direction, cameraRotation, spread);

				state.recoilPitch += weapon.recoilPitch;
				state.recoilYaw += symmetric(weapon.recoilYaw);
				inventory.viewKick = (std::min)(inventory.viewKick + weapon.recoilPitch * 1.5f, 3.0f);

				const Vector3 muzzle = viewModel.hasMuzzle ? viewModel.muzzlePosition : origin + direction * 0.7f;
				spawn(weapon.muzzleFlashPrefab, { muzzle + direction * 0.05f, cameraRotation });
				if (weapon.kind == WeaponKind::Hitscan) fireHitscan(targets, spawn, *engine, ent, inventory, weapon, origin, direction);
				else fireProjectile(spawn, ent, weapon, muzzle, direction, cc ? cc->velocity : Vector3::zero);
			}
		}

	protected:
		void onAttach() override
		{
			// Resolve reload clips once (outside the task graph) and let the clip
			// length define the reload time. The weapons' prefabs (and the ones they
			// refer to: trail, explosion) are loaded here too, so shots read no files.
			auto& resources = *core::Services::get<render::ResourceManager>();
			auto* prefabs = core::Services::find<core::PrefabLibrary>();
			for (auto [ent, inventory] : scene().view<WeaponInventory>())
			{
				WeaponDef* defs[2] = { &inventory.primary, &inventory.secondary };
				for (int slot = 0; slot < 2; ++slot)
				{
					if (prefabs)
						for (const auto* prefab : { &defs[slot]->projectilePrefab, &defs[slot]->impactPrefab, &defs[slot]->muzzleFlashPrefab })
							if (!prefab->empty()) (void)prefabs->load(prefab->path);
					_reloadClips[slot] = {};
					if (defs[slot]->reloadClip.empty()) continue;
					_reloadClips[slot] = resources.get<render::AnimationClip>(defs[slot]->reloadClip);
					if (const auto* clip = resources.getAnimationClip(_reloadClips[slot]); clip && clip->duration() > 0.05f)
						defs[slot]->reloadTime = clip->duration();
				}
				break;
			}
		}

		void startReloadAnimation(const Targets& targets, int slot, const ViewModelState& viewModel)
		{
			if (slot < 0 || slot > 1 || !_reloadClips[slot] || viewModel.activeSlotEntity == core::Entity::invalidID) return;
			auto animator = targets.get<Animator>(viewModel.activeSlotEntity);
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

		void fireHitscan(const Targets& targets, const core::Spawn& spawn, const physics::PhysicsEngine& engine, core::Entity::ID shooter,
			WeaponInventory& inventory, const WeaponDef& weapon, const math::Vector3& origin, const math::Vector3& direction)
		{
			using namespace math;
			const physics::QueryFilter filter = layers::filter(weapon.hitLayers, shooter);
			const auto hit = engine.raycast(origin, direction, weapon.range, filter);
			if (!hit.hit) return;
			if (auto health = targets.get<Health>(hit.entity); health && !health->dead)
			{
				damage(*health, weapon.damage);
				inventory.hitMarker = 0.12f;
			}
			spawn(weapon.impactPrefab, { hit.point + hit.normal * 0.02f });
		}

		// The prefab gives the look and the body template; the weapon's tuning and
		// the shot are applied to the copy. Edits run in the commit: capture values.
		void fireProjectile(const core::Spawn& spawn, core::Entity::ID shooter, const WeaponDef& weapon,
			const math::Vector3& origin, const math::Vector3& direction, const math::Vector3& inheritVelocity)
		{
			using namespace math;
			using namespace physics;
			core::SpawnPose pose;
			pose.position = origin + direction * 0.15f;
			const float size = weapon.projectileRadius * 2.0f * 2.4f; // visual ball bigger than the collider
			pose.scale = Vector3(size, size, size);
			const Vector3 velocity = direction * weapon.projectileSpeed + inheritVelocity * 0.35f;
			spawn(weapon.projectilePrefab, pose)
				.edit<SphereCollider>([radius = weapon.projectileRadius](SphereCollider& collider) { collider.radius = radius; })
				.edit<RigidBody>([gravity = weapon.projectileGravity, velocity](RigidBody& body) {
					body.useGravity = gravity;
					body.linearVelocity = velocity;
				})
				.edit<Projectile>([weapon, shooter](Projectile& projectile) {
					projectile.damage = weapon.damage; projectile.explosionRadius = weapon.explosionRadius;
					projectile.impulse = weapon.explosionImpulse; projectile.lifetime = weapon.projectileLifetime;
					projectile.hitLayers = weapon.hitLayers;
					projectile.owner = shooter;
				});
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
		render::AnimationClipHandle _reloadClips[2];
	};

	REGISTER_SYSTEM(WeaponSystem)
}
CS_STRICT_SYSTEMS_END
