#pragma once
// Clustered light assignment (compute). The view frustum is split into
// X x Y screen tiles and Z exponential depth slices (ClusterGridLayout); each
// froxel gets the list of point lights whose sphere touches its view-space AABB.
// The light pass (deferred) and the forward pass read the same lists.
#include <array>
#include "core/typed_system.h"
#include "dx12_graphic/renderer.h"
#include "dx12_graphic/recorder.h"
#include "dx12_graphic/resource_manager.h"
#include "dx12_graphic/compute_shader.h"
#include "dx12_graphic/camera_render_target.h"
#include "dx12_graphic/lighting/scene_lights.h"
#include "core/registration.h"

CS_STRICT_SYSTEMS_BEGIN
namespace csyren::render
{
    class LightClusterSystem : public core::TypedSystem<LightClusterSystem>
    {
    public:
        static constexpr const char* ShaderPath = "shaders/light_cluster.coshader";

        static constexpr auto phase = "render";
        static constexpr std::array after{ "MeshRenderSystem" };
        static constexpr std::array before{ "LightPassSystem" };
        static constexpr core::GpuTaskScope gpuScope = core::GpuTaskScope::View;
        static constexpr std::array gpu{
            core::GpuUse{ "scene.clusterGrid", D3D12_RESOURCE_STATE_UNORDERED_ACCESS, core::AccessMode::Write },
            core::GpuUse{ "scene.clusterIndices", D3D12_RESOURCE_STATE_UNORDERED_ACCESS, core::AccessMode::Write },
        };

        void execute(Recorder recorder, core::Res<const ResourceManager> resourceManager, core::Res<const SceneLights> sceneLights)
        {
            auto& renderer = *recorder;
            auto& resources = *resourceManager;
            const auto& lights = *sceneLights;
            const auto* target = renderer.currentTarget();
            if (!target) return;
            const auto& engine = *renderer.getEngineVariableBuffer();
            if (engine.lightParams.w == 0.0f) return; // clustering disabled for this view
            if (!_shader) { logMissing(); return; }
            const auto* shader = resources.getComputeShader(_shader);
            if (!shader || !renderer.bindComputeShader(resources, _shader)) return;

            Constants constants{};
            constants.view = engine.viewMatrix;
            constants.screenParams = engine.screenParams;
            constants.clusterParams = engine.clusterParams;
            constants.clusterDepthParams = engine.clusterDepthParams;
            constants.cameraParams = engine.cameraParams;
            constants.lightOffset = lights.directionalCount;
            constants.lightCount = lights.pointCount;
            renderer.setShaderConstants(*shader, "params", &constants, sizeof(constants));
            renderer.setShaderBuffer(*shader, "lights", lights.upload(renderer));
            renderer.setShaderRWBuffer(*shader, "clusterGrid", target->address(ViewAttachment::ClusterGrid));
            renderer.setShaderRWBuffer(*shader, "clusterIndices", target->address(ViewAttachment::ClusterIndices));
            const UINT groupSize = (std::max)(1u, shader->getThreadGroupCountX());
            renderer.dispatch((ClusterGridLayout::Count + groupSize - 1) / groupSize, 1, 1);
        }
    protected:
        void onAttach() override
        {
            if (!_shader) _shader = core::Services::get<ResourceManager>()->get<ComputeShader>(ShaderPath);
        }
    private:
        struct alignas(16) Constants
        {
            DirectX::XMFLOAT4X4 view;          // row_major in HLSL: copied as is
            DirectX::XMFLOAT4 screenParams;
            DirectX::XMFLOAT4 clusterParams;
            DirectX::XMFLOAT4 clusterDepthParams;
            DirectX::XMFLOAT4 cameraParams;
            uint32_t lightOffset, lightCount, pad0, pad1;
        };
        void logMissing()
        {
            if (_warned) return; _warned = true;
            log::error("LightClusterSystem: compute shader '{}' is not available; clustered lighting disabled.", ShaderPath);
        }
        ComputeShaderHandle _shader;
        bool _warned = false;
    };
    CS_SYSTEM(LightClusterSystem);
}
CS_STRICT_SYSTEMS_END
