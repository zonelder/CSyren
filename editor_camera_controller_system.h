#pragma once

#include "core/event_bus.h"
#include "core/services.h"
#include "core/system_base.h"
#include "core/scene.h"
#include "core/camera.h"
#include "math/math.h"
#include "core/time.h"
#include "core/transform.h"

#include "editor_camera_controller.h"


using namespace csyren::core;
using namespace csyren::math;

namespace csyren
{
    class EditorCameraControllerSystem : public core::System
    {
    public:
        explicit EditorCameraControllerSystem() = default;

        void update() override
        {
			using ctx = core::Services;
			auto devices = ctx::get<core::Devices>();
			auto scene = ctx::get<core::Scene>();
			auto time = ctx::get<core::Time>();

            auto& keyboard = devices->keyboard();
            auto& mouse = devices->mouse();

			for (auto [mainCameraID, camera, cameraTransform, editorController] : scene->view<Camera, Transform,csyren::EditorCameraController>())
			{


				if (!mouse.isButtonDown(MouseButton::Left))
					return;
				float speed = editorController.speed;
				float movementSpeed = editorController.movementSpeed;
				auto& delta = mouse.deltaPosition();
				float cam_yaw = speed * delta.x;
				float cam_pitch = speed * delta.y;
				Quaternion yawQuat = Quaternion::angleAxis(cam_yaw, Vector3::up);

				// Calculate the forward vector from the current rotation
				Vector3 forwardVector = cameraTransform.rotation * Vector3::forward;
				Vector3 rightVector = Vector3::up.cross(forwardVector);
				float rightLenSq = rightVector.sqrMagnitude();
				if (rightLenSq > 1e-6f) 
				{
					rightVector.normalize();
				}
				else 
				{
					rightVector = Vector3::right;
				}
				rightVector.normalize();

				Quaternion pitchQuat = Quaternion::angleAxis(cam_pitch, rightVector);

				cameraTransform.rotation *= pitchQuat * yawQuat;

				// Normalize the quaternion to prevent drift
				//cameraTr.rotation = DirectX::XMQuaternionNormalize(cameraTr.rotation);

				// check movving
				Vector3 movement = { 0.0f, 0.0f, 0.0f };
				if (keyboard.isKeyDown(KeyCode::W)) movement[2] = +1.0f;

				if (keyboard.isKeyDown(KeyCode::S)) movement[2] = -1.0f;

				if (keyboard.isKeyDown(KeyCode::A)) movement[0] = -1.0f;
				if (keyboard.isKeyDown(KeyCode::D)) movement[0] = +1.0f;

				if (movement[0] != 0.0f || movement[1] != 0.0f || movement[2] != 0.0f)
				{
					// Get camera's local axes
					const auto& rotation = cameraTransform.rotation;

					Vector3 forward = rotation * Vector3::forward;;
					Vector3 right = rotation * Vector3::right;
					Vector3 up = rotation * Vector3::up;

					// Calculate movement vector in world space
					Vector3 moveVector = right * movement[0] + up * movement[1] + forward * movement[2];
					moveVector.normalize();
					moveVector *= movementSpeed * time->deltaTime();
					cameraTransform.position += moveVector;
				}
		    }

        }

    };

	REGISTER_SYSTEM(EditorCameraControllerSystem);
}
