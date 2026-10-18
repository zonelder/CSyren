#pragma once
#include "camera_render_target.h"
#include "core/camera.h"
#include "core/transform.h"
#include "engine_semantics.h"
#include <atomic>

namespace csyren::render::details
{
    inline EngineVariableBuffer cameraFrame(core::Camera camera,const math::Matrix4x4& invView,UINT width,UINT height,float time)
    {
        using namespace DirectX;
        EngineVariableBuffer frame{};
        camera.aspectRatio = float(width)/(std::max)(1u,height);
        const auto view = XMMatrixInverse(nullptr,invView),projection = camera.projectionMatrix();
        XMStoreFloat4x4(&frame.invViewMatrix,invView); XMStoreFloat4x4(&frame.viewMatrix,view);
        XMStoreFloat4x4(&frame.projectionMatrix,projection);
        XMStoreFloat4x4(&frame.viewProjectionMatrix,XMMatrixMultiply(view,projection));
        XMStoreFloat4(&frame.cameraPosition,XMVector3TransformCoord(XMVectorZero(),invView));
        const auto light = XMVector3TransformNormal(XMVectorSet(.3f,-1,.3f,0),XMMatrixRotationZ(time*XM_2PI/10));
        XMStoreFloat4(&frame.lightDirection,XMVector3Normalize(light)); frame.totalTime = time;
        return frame;
    }
    inline EngineVariableBuffer cameraFrame(core::Camera camera,const core::Transform& transform,UINT width,UINT height,float time)
    { return cameraFrame(camera,transform.localMatrix(),width,height,time); }
    class CameraRenderSchedule
    {
    public:
        void request() { ++_requested; }
        bool due(core::CameraRenderType type, bool resized = false)
        {
            if (type != _type) { _type = type; request(); }
            if (resized) request();
            return type == core::CameraRenderType::EachFrame || _submitted != _requested;
        }
        uint64_t ticket() const { return _requested; }
        void submitted(uint64_t ticket) { _submitted = ticket; }
    private:
        core::CameraRenderType _type = core::CameraRenderType::EachFrame;
        uint64_t _requested = 1,_submitted = 0;
    };
    struct CameraRenderView
    {
        CameraRenderView() : id(++_nextId),target(std::make_shared<CameraRenderTarget>()) {}
        const uint64_t id;
        std::shared_ptr<CameraRenderTarget> target;
        CameraRenderSchedule schedule;
        EngineVariableBuffer engine{};
        math::Vector4 background{};
        bool render = true;
        uint64_t ticket = 0;
    private:
        inline static std::atomic<uint64_t> _nextId{0};
    };
}
