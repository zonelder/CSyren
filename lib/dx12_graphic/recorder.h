#pragma once
// render::Recorder: the execute() parameter of typed GPU passes.
//
//   class TonemapSystem : public core::TypedSystem<TonemapSystem>
//   {
//   public:
//       static constexpr auto phase = "render";
//       static constexpr core::GpuTaskScope gpuScope = core::GpuTaskScope::View;
//       static constexpr std::array gpu{
//           core::GpuUse{ "scene.hdr", D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, core::AccessMode::Read },
//           core::GpuUse{ "scene.color", D3D12_RESOURCE_STATE_RENDER_TARGET, core::AccessMode::Write } };
//
//       void execute(render::Recorder recorder, core::Res<const render::ResourceManager> resources);
//   };
//
// It is the Renderer bound to the task's command list (and to the current
// view's target when gpuScope is View). A system that takes it records GPU work:
// describe() requires a non-empty gpu array, and the frame graph backend makes
// Renderer and PSOFactory task-local for such tasks, so passes recording in
// parallel do not conflict. Res<Renderer> in a pass would instead be a shared
// write that serializes it against every other user of the Renderer.
#include "core/typed_system.h"
#include "dx12_graphic/renderer.h"

namespace csyren::render
{
    class Recorder
    {
    public:
        static void describeParam(core::ParamAccess& access) { access.gpu = true; }
        // Services::get throws if the Renderer is not registered.
        static Recorder makeParam() { return Recorder(*core::Services::get<Renderer>()); }

        Renderer& operator*() const noexcept { return *_renderer; }
        Renderer* operator->() const noexcept { return _renderer; }

    private:
        explicit Recorder(Renderer& renderer) noexcept : _renderer(&renderer) {}
        Renderer* _renderer;
    };
}
