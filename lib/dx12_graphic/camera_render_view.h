#pragma once
#include "camera_render_target.h"
#include "core/camera.h"
#include "core/transform.h"
#include "engine_semantics.h"
#include <atomic>
#include <cmath>

namespace csyren::render
{
    // Scene-wide lighting inputs gathered once per frame (LightCollectSystem)
    // and copied into every camera view's constants.
    struct RenderEnvironment
    {
        DirectX::XMFLOAT4 lightDirection{ 0.3f, -1.0f, 0.3f, 0.0f }; // first directional light
        DirectX::XMFLOAT4 sunColor{ 0, 0, 0, 0 };
        DirectX::XMFLOAT4 ambientColor{ 0.2f, 0.2f, 0.22f, 0 };
        float exposure = 1.0f;
        uint32_t directionalCount = 0;
        uint32_t pointCount = 0;
        int debugView = 0;
        bool clustered = true;
    };
}

namespace csyren::render::details
{
    inline EngineVariableBuffer cameraFrame(core::Camera camera,const math::Matrix4x4& invView,UINT width,UINT height,float time,
        const RenderEnvironment& environment)
    {
        using namespace DirectX;
        EngineVariableBuffer frame{};
        camera.aspectRatio = float(width)/(std::max)(1u,height);
        const auto view = XMMatrixInverse(nullptr,invView),projection = camera.projectionMatrix();
        const auto viewProjection = XMMatrixMultiply(view,projection);
        XMStoreFloat4x4(&frame.invViewMatrix,invView); XMStoreFloat4x4(&frame.viewMatrix,view);
        XMStoreFloat4x4(&frame.projectionMatrix,projection);
        XMStoreFloat4x4(&frame.viewProjectionMatrix,viewProjection);
        XMStoreFloat4x4(&frame.invProjectionMatrix,XMMatrixInverse(nullptr,projection));
        XMStoreFloat4x4(&frame.invViewProjectionMatrix,XMMatrixInverse(nullptr,viewProjection));
        XMStoreFloat4(&frame.cameraPosition,XMVector3TransformCoord(XMVectorZero(),invView));
        frame.totalTime = time;
        frame.lightDirection = environment.lightDirection;
        frame.sunColor = environment.sunColor;
        frame.ambientColor = { environment.ambientColor.x,environment.ambientColor.y,environment.ambientColor.z,environment.exposure };
        frame.backgroundColor = { camera.background.x,camera.background.y,camera.background.z,camera.background.w };
        frame.screenParams = { float(width),float(height),1.0f/float((std::max)(1u,width)),1.0f/float((std::max)(1u,height)) };
        const float nearDistance = (std::min)(camera.near,camera.far),farDistance = (std::max)(camera.near,camera.far);
        frame.cameraParams = { nearDistance,farDistance,frame.projectionMatrix._11,frame.projectionMatrix._22 };
        const bool perspective = camera.projection == core::ProjectionType::Perspective;
        const float clustered = environment.clustered && perspective ? 1.0f : 0.0f;
        frame.lightParams = { float(environment.directionalCount),float(environment.pointCount),float(environment.debugView),clustered };
        frame.clusterParams = { float(ClusterGridLayout::X),float(ClusterGridLayout::Y),float(ClusterGridLayout::Z),float(ClusterGridLayout::MaxLightsPerCluster) };
        const float range = std::log2((std::max)(farDistance/(std::max)(nearDistance,1e-4f),1.0001f));
        const float scale = float(ClusterGridLayout::Z)/range;
        frame.clusterDepthParams = { scale,-std::log2((std::max)(nearDistance,1e-4f))*scale,range,0.0f };
        return frame;
    }
    inline EngineVariableBuffer cameraFrame(core::Camera camera,const core::Transform& transform,UINT width,UINT height,float time,
        const RenderEnvironment& environment)
    { return cameraFrame(camera,transform.localMatrix(),width,height,time,environment); }
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
        bool render = true;      // recorded this frame (due and shown)
        bool hidden = false;     // due but not recorded, because nobody shows it
        // Set by whoever shows the output (the editor's Scene view): a view that is
        // not shown is not rendered; a pending render waits until it is shown again.
        bool shown = true;
        uint64_t ticket = 0;
    private:
        inline static std::atomic<uint64_t> _nextId{0};
    };
}
