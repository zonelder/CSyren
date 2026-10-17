#pragma once
#include "math/vector2.h"
#include "core/camera.h"
#include "core/transform.h"
#include "editor_camera_controller.h"
#include <algorithm>
#include <cmath>

namespace csyren::editor
{
    enum class ViewportCamera { Scene, Game };
    struct ViewportImage
    {
        float x = 0,y = 0,width = 0,height = 0;
        bool hovered = false,focused = false,visible = false;
        bool contains(float px,float py) const
        { return visible && width > 0 && height > 0 && px >= x && py >= y && px < x+width && py < y+height; }
        math::Vector2 ndc(float px,float py) const
        { return {2*(px-x)/std::max(width,1.f)-1,1-2*(py-y)/std::max(height,1.f)}; }
        static ViewportImage fit(float x,float y,float width,float height,float aspect)
        {
            if (!std::isfinite(width) || !std::isfinite(height) || !std::isfinite(aspect) || width < 1 || height < 1 || aspect <= 0) return {};
            const float w = std::min(width,height*aspect), h = w/aspect;
            return {x+(width-w)*.5f,y+(height-h)*.5f,w,h,false,false,true};
        }
    };
    struct ViewportNavigation
    {
        bool captured = false,active = false;
        float dx = 0,dy = 0,forward = 0,right = 0;
    };
    enum class GizmoMode { Move, Rotate, Scale };
    struct EditorViewportState
    {
        core::Camera camera;
        core::Transform transform;
        EditorCameraController controller;
        GizmoMode gizmoMode = GizmoMode::Move;
        ViewportImage viewport;
        ViewportImage gameViewport;
        ViewportNavigation navigation;
        bool viewportInput = false;
        EditorViewportState()
        {
            camera.near = 0.1f; camera.far = 1000.0f;
            transform.position = math::Vector3{ 0.0f, 2.0f, -4.0f };
            controller.movementSpeed = 3.0f;
        }
    };
}

