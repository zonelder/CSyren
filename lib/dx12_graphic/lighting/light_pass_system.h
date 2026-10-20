#pragma once
// Deferred light pass: fullscreen triangle reading the GBuffer and depth,
// writing linear HDR radiance to scene.hdr (sky = camera background where
// depth is cleared). Point lights come from the cluster lists.
#include "core/system_base.h"
#include "core/services.h"
#include "dx12_graphic/renderer.h"
#include "dx12_graphic/resource_manager.h"
#include "dx12_graphic/camera_render_target.h"
#include "dx12_graphic/lighting/scene_lights.h"
#include "dx12_graphic/lighting/fullscreen_pass.h"
#include "core/registration.h"

namespace csyren::render
{
    class LightPassSystem : public core::System
    {
    public:
        static constexpr const char* ShaderPath = "shaders/light_pass.shader";

        core::TaskDescription describe() const override
        {
            core::TaskDescription d; d.phase = "render";
            d.after = { "MeshRenderSystem", "LightClusterSystem" };
            d.gpuScope = core::GpuTaskScope::View;
            d.readResource<ResourceManager>().readResource<SceneLights>();
            d.gpu("scene.gbuffer0", D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, core::AccessMode::Read);
            d.gpu("scene.gbuffer1", D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, core::AccessMode::Read);
            d.gpu("scene.gbuffer2", D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, core::AccessMode::Read);
            d.gpu("scene.depth", D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, core::AccessMode::Read);
            d.gpu("scene.clusterGrid", D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, core::AccessMode::Read);
            d.gpu("scene.clusterIndices", D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, core::AccessMode::Read);
            d.gpu("scene.hdr", D3D12_RESOURCE_STATE_RENDER_TARGET, core::AccessMode::Write);
            return d;
        }

        void execute() override
        {
            auto& renderer = *core::Services::get<Renderer>();
            auto& resources = *core::Services::get<const ResourceManager>();
            const auto& lights = *core::Services::get<const SceneLights>();
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
