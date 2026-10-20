#pragma once
// First-person weapon view model: shows the active weapon slot, and layers
// procedural motion (mouse sway, walk bob, recoil kick, weapon switch dip,
// reload dip/tilt, slide tilt) on top of any Animator clips playing on the
// weapon models. Publishes the muzzle position for the weapon code.
#include <algorithm>
#include <cmath>
#include "core/system_base.h"
#include "core/services.h"
#include "core/scene.h"
#include "core/transform.h"
#include "core/transform_hierarchy.h"
#include "core/time.h"
#include "cstdmf/tree_algorithms.h"
#include "math/math.h"
#include "physics/character_controller.h"

#include "game_components.h"
#include "player_input.h"

namespace csyren::game
{
	class ViewModelSystem : public core::System
	{
	public:
		core::TaskDescription describe() const override
		{
			core::TaskDescription d; d.phase = "simulation"; d.after = { "CameraRigSystem" };
			d.write<ViewModel>().write<ViewModelSlot>().write<core::Transform>()
				.read<PlayerTag>().read<PilotState>().read<PilotConfig>().read<WeaponInventory>().read<physics::CharacterController>();
			d.readResource<PlayerInput>().readResource<core::Time>().readResource<core::SceneEntities>().writeResource<ViewModelState>();
			return d;
		}

		void execute() override
		{
			using namespace math;
			const auto& in = *core::Services::get<const PlayerInput>();
			const auto* time = core::Services::get<const core::Time>();
			auto& state = *core::Services::get<ViewModelState>();
			const float dt = std::clamp(time->deltaTime(), 0.0f, 0.05f);
			auto& scene = this->scene();

			// Player state the view model reacts to.
			const WeaponInventory* inventory = nullptr; const PilotState* pilot = nullptr; const PilotConfig* config = nullptr;
			const physics::CharacterController* controller = nullptr;
			for (auto [ent, tag, pilotState, inv, cfg] : scene.view<const PlayerTag, const PilotState, const WeaponInventory, const PilotConfig>())
			{
				inventory = &inv; pilot = &pilotState; config = &cfg;
				if (auto cc = scene.getComponent<const physics::CharacterController>(ent)) controller = cc.get();
				break;
			}
			state.hasMuzzle = false; state.switching = false; state.activeSlotEntity = core::Entity::invalidID;
			if (!inventory || !pilot || !config) return;

			int shownSlot = inventory->active;
			for (auto [ent, vm, tr] : scene.view<ViewModel, core::Transform>())
			{
				// Weapon switch: drop, swap the visible model halfway, raise.
				if (vm.shownSlot < 0) vm.shownSlot = inventory->active;
				if (inventory->active != vm.shownSlot && vm.switchTimer <= 0.0f) { vm.switchTimer = vm.switchTime; vm.switchTarget = inventory->active; }
				float drop = 0.0f;
				if (vm.switchTimer > 0.0f)
				{
					vm.switchTimer = (std::max)(0.0f, vm.switchTimer - dt);
					const float t = 1.0f - vm.switchTimer / (std::max)(vm.switchTime, 0.01f);
					if (t >= 0.5f) vm.shownSlot = vm.switchTarget;
					drop = std::sin(t * DirectX::XM_PI) * vm.switchDrop;
					state.switching = true;
				}
				shownSlot = vm.shownSlot;

				// Mouse sway (position lags the look, rotation leads it).
				const float lookX = in.captured ? in.lookDX : 0.0f, lookY = in.captured ? in.lookDY : 0.0f;
				const float smoothing = (std::min)(1.0f, dt * vm.swaySmoothing);
				vm.swayX += (std::clamp(-lookX * vm.swayPosition, -0.04f, 0.04f) - vm.swayX) * smoothing;
				vm.swayY += (std::clamp(lookY * vm.swayPosition, -0.04f, 0.04f) - vm.swayY) * smoothing;
				vm.swayYaw += (std::clamp(-lookX * vm.swayRotation, -5.0f, 5.0f) - vm.swayYaw) * smoothing;
				vm.swayPitch += (std::clamp(-lookY * vm.swayRotation, -5.0f, 5.0f) - vm.swayPitch) * smoothing;

				// Walk bob.
				const bool grounded = controller && controller->grounded && pilot->move == PilotMove::Ground;
				const float speedRatio = std::clamp(pilot->speed / (std::max)(config->ground.runSpeed, 0.1f), 0.0f, 1.6f);
				const float bobTarget = grounded && pilot->speed > 0.6f ? speedRatio : 0.0f;
				vm.bobWeight += (bobTarget - vm.bobWeight) * (std::min)(1.0f, dt * 8.0f);
				if (bobTarget > 0.0f) vm.bobPhase += dt * vm.bobFrequency * speedRatio * DirectX::XM_PI;
				const float bobX = std::sin(vm.bobPhase) * vm.bobAmount * vm.bobWeight;
				const float bobY = std::abs(std::cos(vm.bobPhase)) * vm.bobAmount * 0.6f * vm.bobWeight;

				// Recoil (viewKick is 0..3, decays in WeaponSystem).
				const float kick = std::clamp(inventory->viewKick / 3.0f, 0.0f, 1.0f);
				// Reload envelope.
				float reload = 0.0f;
				if (inventory->reloading)
				{
					const auto& weapon = inventory->active == 0 ? inventory->primary : inventory->secondary;
					const float total = (std::max)(weapon.reloadTime, 0.01f);
					const float progress = std::clamp(1.0f - inventory->reloadTimer / total, 0.0f, 1.0f);
					reload = std::sin((std::min)(progress, 1.0f) * DirectX::XM_PI);
					reload = (std::min)(reload * 3.0f, 1.0f); // fast in, long hold, fast out
				}
				const float stateTilt = pilot->move == PilotMove::Slide ? -vm.slideTilt
					: pilot->move == PilotMove::WallRun ? vm.slideTilt * 0.5f * float(-pilot->wallSide) : 0.0f;

				const Vector3 position = vm.offset + Vector3(vm.swayX + bobX, vm.swayY + bobY - drop - vm.reloadDrop * reload, -vm.recoilBack * kick);
				const Quaternion rotation = Quaternion::euler(
					-vm.recoilUp * kick + vm.reloadTilt * reload + drop * 90.0f + vm.swayPitch,
					vm.swayYaw - vm.reloadTilt * 0.5f * reload,
					stateTilt + vm.swayYaw * 0.5f);
				if (!tr.position.exactEqual(position) || !tr.rotation.exactEqual(rotation))
				{
					tr.position = position; tr.rotation = rotation;
					scene.patch<core::Transform>(ent);
				}
				break; // one view model
			}

			// Show the active slot only; find its muzzle.
			const uint64_t revision = scene.hierarchyRevision();
			for (auto [ent, slot, tr] : scene.view<ViewModelSlot, core::Transform>())
			{
				if (!slot.baseScaleCaptured) { slot.baseScale = tr.scale; slot.baseScaleCaptured = true; }
				const bool visible = slot.slot == shownSlot;
				const Vector3 scale = visible ? slot.baseScale : Vector3::zero;
				if (!tr.scale.exactEqual(scale)) { tr.scale = scale; scene.patch<core::Transform>(ent); }
				if (!visible) continue;
				state.activeSlotEntity = ent;
				if (slot.muzzle == core::Entity::invalidID || slot.muzzleRevision != revision)
				{
					slot.muzzle = core::Entity::invalidID; slot.muzzleRevision = revision;
					const auto& entities = scene.entities();
					cstdmf::tree::preorder(ent, [&](auto id) -> const auto& { return entities.try_get(id)->children; }, [&](auto id) {
						if (slot.muzzle == core::Entity::invalidID)
							if (const auto* entity = entities.try_get(id); entity && entity->name == slot.muzzleNode) slot.muzzle = id;
					});
				}
				if (slot.muzzle != core::Entity::invalidID && scene.entities().contains(slot.muzzle))
				{
					const auto world = core::worldMatrix(scene, slot.muzzle);
					state.hasMuzzle = true;
					state.muzzlePosition = world.translation();
					state.muzzleForward = world.forward();
				}
			}
		}
	};

	REGISTER_SYSTEM(ViewModelSystem)
}
