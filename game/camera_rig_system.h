#pragma once
// Drives the first-person camera child of the pilot: pitch, wallrun roll,
// crouch eye height, landing dip, recoil kick and speed-based FOV.
#include <algorithm>
#include <cmath>
#include "core/typed_system.h"
#include "core/transform.h"
#include "core/camera.h"
#include "core/time.h"
#include "math/math.h"
#include "physics/character_controller.h"

#include "game_components.h"

CS_STRICT_SYSTEMS_BEGIN
namespace csyren::game
{
	class CameraRigSystem : public core::TypedSystem<CameraRigSystem>
	{
	public:
		static constexpr auto phase = "simulation";
		static constexpr auto after = "PhysicsSystem";

		void execute(core::Update<const PilotConfig, PilotState, const physics::CharacterController> pilots,
			core::Update<core::Transform, core::Camera> cameras,
			core::Select<WeaponInventory> inventories,
			core::Res<const core::Time> time)
		{
			using namespace math;
			const float dt = std::clamp(time->deltaTime(), 0.0f, 0.05f);
			for (auto [ent, config, state, cc] : pilots)
			{
				if (state.cameraEntity == core::Entity::invalidID) continue;
				auto camTr = cameras.get<core::Transform>(state.cameraEntity);
				auto camera = cameras.get<core::Camera>(state.cameraEntity);
				if (!camTr || !camera) continue;
				const auto& look = config.look;

				// Eye height follows the capsule height (crouch / slide).
				const float headroom = config.slide.standHeight - look.eyeHeight; // eyes stay this far below the capsule top
				const float targetEye = (std::max)(look.crouchEyeHeight, cc.currentHeight - headroom);
				_eye += (targetEye - _eye) * (std::min)(1.0f, dt * 14.0f);

				// Landing dip: a damped spring driven by the absorbed vertical speed.
				if (state.landImpulse > 0.0f)
				{
					_dipVelocity -= (std::min)(state.landImpulse, 18.0f) * 0.06f;
					state.landImpulse = 0.0f;
				}
				const float stiffness = 160.0f, damping = 16.0f;
				_dipVelocity += (-_dip * stiffness - _dipVelocity * damping) * dt;
				_dip += _dipVelocity * dt;

				// Wallrun roll.
				_roll += (state.cameraRollTarget - _roll) * (std::min)(1.0f, dt * 9.0f);

				// Recoil kick (visual only, the aim recoil lives in PilotState).
				float kick = 0.0f;
				if (auto inventory = inventories.get<WeaponInventory>(ent)) kick = inventory->viewKick;
				_kick += (kick - _kick) * (std::min)(1.0f, dt * 25.0f);

				const Quaternion pitchRotation = Quaternion::angleAxis(state.pitch - _kick, Vector3::right);
				const Quaternion rollRotation = Quaternion::angleAxis(_roll, Vector3::forward);
				const Quaternion local = rollRotation * pitchRotation;
				const Vector3 position(0.0f, _eye + _dip, 0.0f);
				if (!camTr->rotation.exactEqual(local) || !camTr->position.exactEqual(position))
					cameras.patch(camTr, [&](core::Transform& t) { t.rotation = local; t.position = position; });

				// FOV kick with speed.
				const float excess = std::clamp((state.speed - config.ground.runSpeed) / 8.0f, 0.0f, 1.0f);
				const float targetFov = look.baseFov + look.speedFovKick * excess + (state.move == PilotMove::Slide ? 2.0f : 0.0f);
				camera->fov += (targetFov - camera->fov) * (std::min)(1.0f, dt * 8.0f);
			}
		}

	protected:
		void onAttach() override { _eye = 1.62f; _dip = _dipVelocity = _roll = _kick = 0.0f; }

	private:
		float _eye = 1.62f, _dip = 0.0f, _dipVelocity = 0.0f, _roll = 0.0f, _kick = 0.0f;
	};

	REGISTER_SYSTEM(CameraRigSystem)
}
CS_STRICT_SYSTEMS_END
