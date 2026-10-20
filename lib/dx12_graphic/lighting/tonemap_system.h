#pragma once
// HDR -> LDR: exposure, ACES tone curve and gamma encoding into scene.color.
// Debug views (LightingSettings::debugView) pass through untouched.
#include "core/system_base.h"
#include "core/services.h"
#include "dx12_graphic/renderer.h"
#include "dx12_graphic/resource_manager.h"
#include "dx12_graphic/camera_render_target.h"
#include "dx12_graphic/lighting/fullscreen_pass.h"
#include "core/registration.h"

namespace csyren::render
{
    class TonemapSystem : public core::System
    {
    public:
        static constexpr const char* ShaderPath = "shaders/tonemap.shader";

        core::TaskDescription describe() const override
        {
            core::TaskDescription d; d.phase = "render";
            d.after = { "ForwardRenderSystem" };
            d.gpuScope = core::GpuTaskScope::View;
            d.readResource<ResourceManager>();
            d.gpu("scene.hdr", D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, core::AccessMode::Read);
            d.gpu("scene.color", D3D12_RESOURCE_STATE_RENDER_TARGET, core::AccessMode::Write);
            return d;
        }

        void execute() override
        {
            auto& renderer = *core::Services::get<Renderer>();
            auto& resources = *core::Services::get<const ResourceManager>();
            const auto* target = renderer.currentTarget();
            if (!target) return;
            const auto* shader = _pass.bind(renderer, resources);
            if (!shader) return;
            renderer.setShaderTexture(*shader, "hdrTexture", target->srv(ViewAttachment::Hdr));
            renderer.drawFullscreen();
        }
    protected:
        void onAttach() override { _pass.load(ShaderPath); }
    private:
        FullscreenPass _pass;
    };
    CS_SYSTEM(TonemapSystem);
}
