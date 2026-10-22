#pragma once
// HDR -> LDR: exposure, ACES tone curve and gamma encoding into scene.color.
// Debug views (LightingSettings::debugView) pass through untouched.
#include <array>
#include "core/typed_system.h"
#include "dx12_graphic/renderer.h"
#include "dx12_graphic/recorder.h"
#include "dx12_graphic/resource_manager.h"
#include "dx12_graphic/camera_render_target.h"
#include "dx12_graphic/lighting/fullscreen_pass.h"
#include "core/registration.h"

CS_STRICT_SYSTEMS_BEGIN
namespace csyren::render
{
    class TonemapSystem : public core::TypedSystem<TonemapSystem>
    {
    public:
        static constexpr const char* ShaderPath = "shaders/tonemap.shader";

        static constexpr auto phase = "render";
        static constexpr std::array after{ "ForwardRenderSystem" };
        static constexpr core::GpuTaskScope gpuScope = core::GpuTaskScope::View;
        static constexpr std::array gpu{
            core::GpuUse{ "scene.hdr", D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, core::AccessMode::Read },
            core::GpuUse{ "scene.color", D3D12_RESOURCE_STATE_RENDER_TARGET, core::AccessMode::Write },
        };

        void execute(Recorder recorder, core::Res<const ResourceManager> resourceManager)
        {
            auto& renderer = *recorder;
            auto& resources = *resourceManager;
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
CS_STRICT_SYSTEMS_END
