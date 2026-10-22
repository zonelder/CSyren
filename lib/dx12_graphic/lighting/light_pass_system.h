#pragma once
// Deferred light pass: fullscreen triangle reading the GBuffer and depth,
// writing linear HDR radiance to scene.hdr (sky = camera background where
// depth is cleared). Point lights come from the cluster lists.
#include <array>
#include "core/typed_system.h"
#include "dx12_graphic/renderer.h"
#include "dx12_graphic/recorder.h"
#include "dx12_graphic/resource_manager.h"
#include "dx12_graphic/camera_render_target.h"
#include "dx12_graphic/lighting/scene_lights.h"
#include "dx12_graphic/lighting/fullscreen_pass.h"
#include "core/registration.h"

CS_STRICT_SYSTEMS_BEGIN
namespace csyren::render
{
    class LightPassSystem : public core::TypedSystem<LightPassSystem>
    {
    public:
        static constexpr const char* ShaderPath = "shaders/light_pass.shader";

        static constexpr auto phase = "render";
        static constexpr std::array after{ "MeshRenderSystem", "LightClusterSystem" };
        static constexpr core::GpuTaskScope gpuScope = core::GpuTaskScope::View;
        static constexpr std::array gpu{
            core::GpuUse{ "scene.gbuffer0", D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, core::AccessMode::Read },
            core::GpuUse{ "scene.gbuffer1", D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, core::AccessMode::Read },
            core::GpuUse{ "scene.gbuffer2", D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, core::AccessMode::Read },
            core::GpuUse{ "scene.depth", D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, core::AccessMode::Read },
            core::GpuUse{ "scene.clusterGrid", D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, core::AccessMode::Read },
            core::GpuUse{ "scene.clusterIndices", D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, core::AccessMode::Read },
            core::GpuUse{ "scene.hdr", D3D12_RESOURCE_STATE_RENDER_TARGET, core::AccessMode::Write },
        };

        void execute(Recorder recorder, core::Res<const ResourceManager> resourceManager, core::Res<const SceneLights> sceneLights)
        {
            auto& renderer = *recorder;
            auto& resources = *resourceManager;
            const auto& lights = *sceneLights;
            const auto* target = renderer.currentTarget();
            if (!target) return;
            const auto* shader = _pass.bind(renderer, resources);
            if (!shader) return;
            renderer.setShaderTexture(*shader, "gbuffer0", target->srv(ViewAttachment::GBuffer0));
            renderer.setShaderTexture(*shader, "gbuffer1", target->srv(ViewAttachment::GBuffer1));
            renderer.setShaderTexture(*shader, "gbuffer2", target->srv(ViewAttachment::GBuffer2));
            renderer.setShaderTexture(*shader, "depthTexture", target->srv(ViewAttachment::Depth));
            renderer.setShaderBuffer(*shader, "lights", lights.upload(renderer));
            renderer.setShaderBuffer(*shader, "clusterGrid", target->address(ViewAttachment::ClusterGrid));
            renderer.setShaderBuffer(*shader, "clusterIndices", target->address(ViewAttachment::ClusterIndices));
            renderer.drawFullscreen();
        }
    protected:
        void onAttach() override { _pass.load(ShaderPath); }
    private:
        FullscreenPass _pass;
    };
    CS_SYSTEM(LightPassSystem);
}
CS_STRICT_SYSTEMS_END
