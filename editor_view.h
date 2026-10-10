#pragma once
#include "core/camera.h"
#include "core/transform.h"
#include "editor_camera_controller.h"

namespace csyren::editor
{
    struct EditorView
    {
        core::Camera camera;
        core::Transform transform;
        EditorCameraController controller;
        EditorView()
        {
            camera.near = 0.1f; camera.far = 1000.0f;
            transform.position = math::Vector3{ 0.0f, 2.0f, -4.0f };
            controller.movementSpeed = 3.0f;
        }
    };
}
