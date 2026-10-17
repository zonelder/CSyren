#pragma once

#include "core/event_bus.h"
#include "core/services.h"
#include "core/system_base.h"
#include "core/scene.h"
#include "core/camera.h"
#include "math/math.h"
#include "core/time.h"
#include "core/devices.h"
#include "core/transform.h"

#include "editor_camera_controller.h"
#include "editor_view.h"


using namespace csyren::core;
using namespace csyren::math;

namespace csyren
{
    class EditorCameraControllerSystem : public core::System
    {
    public:
        core::TaskDescription describe() const override {
            core::TaskDescription d; d.phase = "simulation";
            // Input is a snapshot after the input phase. Only EditorView is mutated;
            // this controller neither calls window APIs nor accesses scene components.
            d.readResource<core::Devices>().readResource<core::Time>().writeResource<editor::EditorView>(); return d;
        }

        explicit EditorCameraControllerSystem() = default;

        void execute() override
        {
			using ctx = core::Services;
			auto devices = ctx::get<const core::Devices>();
			auto editorView = ctx::get<editor::EditorView>();
			auto time = ctx::get<const core::Time>();

            auto& keyboard = devices->keyboard();
            auto& mouse = devices->mouse();

			if (editorView)
			{


				auto& cameraTransform = editorView->transform;
                auto& editorController = editorView->controller;
                if (!mouse.isButtonDown(MouseButton::Right))
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
