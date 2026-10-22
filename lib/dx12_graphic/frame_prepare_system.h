#pragma once
#include <array>
#include "core/typed_system.h"
#include "core/registration.h"
#include "core/camera.h"
#include "core/transform.h"
#include "core/time.h"
#include "core/window.h"
#include "dx12_graphic/recorder.h"
#include "dx12_graphic/resource_manager.h"
#include "dx12_graphic/descriptors.h"
#include "dx12_graphic/gpu_resources.h"

namespace csyren::render
{
    namespace details { struct PresentationState; struct FrameRecording; }

    // Acquires the frame slot, prepares the camera views and uploads pending
    // resources; leaves the frame token (FrameRecording) "ready" for GPU passes.
    class FrameAcquireSystem : public core::TypedSystem<FrameAcquireSystem> {
    public:
        static constexpr auto phase = "acquire";
        static constexpr core::TaskAffinity affinity = core::TaskAffinity::MainThread;
        static constexpr std::array states{ core::StateUse::write<details::FrameRecording>("", "ready") };

        void init() override;
        void shutdown() override;
        // DescriptorManager and GpuResources are used by the camera targets, the
        // upload pipeline and the GPU plan that this system drives.
        void execute(Renderer::CameraQuery cameras, core::Res<Renderer> renderer, core::Res<ResourceManager> resources,
            core::Res<DescriptorManager> descriptors, core::Res<const GpuResources> imports,
            core::Res<const core::Window> window, core::Res<const core::Time> time);
    private:
        void onAttach() override;
        void onDetach() noexcept override;
        std::shared_ptr<details::PresentationState> _state;
    };
    CS_SYSTEM(FrameAcquireSystem);

CS_STRICT_SYSTEMS_BEGIN
    class FramePrepareSystem : public core::TypedSystem<FramePrepareSystem>
    {
    public:
        static constexpr auto phase = "prepare";
        static constexpr core::GpuTaskScope gpuScope = core::GpuTaskScope::View;
        // Clears the deferred geometry targets. scene.hdr / scene.color are
        // fully written by the light pass and the tonemap pass.
        static constexpr std::array gpu{
            core::GpuUse{ "scene.gbuffer0", D3D12_RESOURCE_STATE_RENDER_TARGET, core::AccessMode::Write },
            core::GpuUse{ "scene.gbuffer1", D3D12_RESOURCE_STATE_RENDER_TARGET, core::AccessMode::Write },
            core::GpuUse{ "scene.gbuffer2", D3D12_RESOURCE_STATE_RENDER_TARGET, core::AccessMode::Write },
            core::GpuUse{ "scene.depth", D3D12_RESOURCE_STATE_DEPTH_WRITE, core::AccessMode::Write },
        };

        void init() override;
        void execute(Recorder recorder);
        void shutdown() override;
    private:
        std::shared_ptr<details::PresentationState> _state;
    };

    CS_SYSTEM(FramePrepareSystem);
CS_STRICT_SYSTEMS_END
}
