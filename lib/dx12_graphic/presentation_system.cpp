#include "pch.h"
#include "cstdmf/profiler.h"
#include "presentation_system.h"
#include "frame_prepare_system.h"
#include "presentation_state.h"
#include "frame_graph_backend.h"
#include "camera_render_target.h"
#include "core/system_manager.h"
#include "resource_manager.h"
#include "core/scene.h"
#include "core/time.h"
#include "core/window.h"
#include "core/camera.h"
#include "core/camera_service.h"
#include "core/transform.h"
#include "math/matrix4x4.h"
#include <stdexcept>
#include <algorithm>
#include <chrono>

namespace csyren::render
{
    namespace
    {
        void require(bool success, const char* message)
        {
            if (!success) throw std::runtime_error(message);
        }

        void transition(CommandList& commands, ID3D12Resource* resource,
                        D3D12_RESOURCE_STATES before, D3D12_RESOURCE_STATES after)
        {
            D3D12_RESOURCE_BARRIER barrier{};
            barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
            barrier.Transition.pResource = resource;
            barrier.Transition.StateBefore = before;
            barrier.Transition.StateAfter = after;
            barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
            commands.resourceBarrier(barrier);
        }
    }

    namespace
    {
        std::shared_ptr<details::PresentationState> acquireState()
        {
            // Resource ownership stays with the systems. This weak reference
            // only connects independently constructed system instances.
            static std::weak_ptr<details::PresentationState> shared;
            if (auto state = shared.lock()) return state;
            auto state = std::make_shared<details::PresentationState>();
            state->init();
            shared = state;
            return state;
        }
    }

    void FrameAcquireSystem::init() {
        _state = acquireState();
        if (!_state->graph) _state->graph = std::make_shared<details::FrameGraphBackend>(*_state, core::Services::get<Renderer>()->queue());
        core::Services::get<core::SystemManager>()->taskBackend(_state->graph);
    }
    void FrameAcquireSystem::shutdown() { _state.reset(); }
    void FrameAcquireSystem::onAttach() { core::Services::get<Renderer>()->attachCameraScene(scene()); }
    void FrameAcquireSystem::onDetach() noexcept { core::Services::get<Renderer>()->detachCameraScene(); }
    void FramePrepareSystem::init() { _state = acquireState(); }
    void PresentationSystem::init() { _state = acquireState(); }
    void FramePrepareSystem::shutdown() { _state.reset(); }
    void PresentationSystem::shutdown() { _state.reset(); }

CS_STRICT_SYSTEMS_BEGIN
    // DescriptorManager and GpuResources are declaration only (unnamed): the camera
    // targets, the upload pipeline and the GPU plan fetch them through Services.
    void FrameAcquireSystem::execute(Renderer::CameraQuery cameras, core::Res<Renderer> renderer, core::Res<ResourceManager> resources,
        core::Res<DescriptorManager>, core::Res<const GpuResources>,
        core::Res<const core::Window> window, core::Res<const core::Time> time)
    {
        auto& context = *_state;
        renderer->collectDeferredReleases();
        context.resize(static_cast<UINT>(window->width()),static_cast<UINT>(window->height()));
        context.commandIndex = (context.commandIndex + 1) % Renderer::FramesInFlight;
        {
            CS_PROFILE_SCOPE("Wait GPU frame slot");
            context.graph->acquire();
        }
        UINT target = details::PresentationState::FrameCount;
        {
            std::lock_guard lock(context.mutex);
            if (context.outputError) std::rethrow_exception(context.outputError);
            UINT keep = details::PresentationState::FrameCount;
            UINT64 newest = context.lastCopied;
            for (UINT i = 0; i < details::PresentationState::FrameCount; ++i)
            {
                auto& candidate = context.frames[i];
                if (candidate.sequence > newest && renderer->queue().isComplete(candidate.renderFence))
                {
                    newest = candidate.sequence;
                    keep = i;
                }
            }
            for (UINT i = 0; i < details::PresentationState::FrameCount; ++i)
            {
                const auto& candidate = context.frames[i];
                if (i != keep && !candidate.copying && !candidate.recording
                    && (target == details::PresentationState::FrameCount || candidate.sequence < context.frames[target].sequence))
                    target = i;
            }
            require(target != details::PresentationState::FrameCount, "No writable mailbox color buffer");
            context.frameIndex = target;
            context.frames[target].recording = true;
            context.frames[target].sequence = 0;
        }
        auto& frame = context.frames[target];
        float background[]{ 0.1f, 0.1f, 0.3f, 1.0f };
        {
            CS_PROFILE_SCOPE("Prepare camera views");
            renderer->prepareCameraViews(cameras, time->totalTime(), context.width, context.height);
        }
        // Stable colors reuse the texture without waiting for its previous draw:
        // both writes are already ordered on the same GPU queue.
        if (frame.color && !std::equal(background, background + 4, frame.clearColor)) {
            CS_PROFILE_SCOPE("Wait previous draw");
            renderer->queue().syncAwaitComplete(frame.renderFence);
        }
        context.prepareTarget(frame, background);
        std::copy(background, background + 4, context.background);
        {
            CS_PROFILE_SCOPE("Resource uploads");
            resources->update();
        }
        CS_PROFILE_SCOPE("Prepare GPU views");
        context.graph->prepareViews();
    }
CS_STRICT_SYSTEMS_END

CS_STRICT_SYSTEMS_BEGIN
    void FramePrepareSystem::execute(Recorder recorder) {
        // GBuffer contents outside geometry are ignored (depth == 0 marks the sky).
        const float zero[4]{ 0, 0, 0, 0 };
        recorder->clearColor(zero);
        recorder->clearDepth();
    }
CS_STRICT_SYSTEMS_END

CS_STRICT_SYSTEMS_BEGIN
    // The Renderer is declaration only (unnamed): the backend's submit() records into it.
    void PresentationSystem::execute(core::Res<Renderer>)
    {
        auto& context = *_state;
        auto& frame = context.frames[context.frameIndex];
        uint64_t renderFence = 0;
        {
            CS_PROFILE_SCOPE("GPU submit");
            renderFence = context.graph->submit();
        }
        {
            std::lock_guard lock(context.mutex);
            frame.sequence = ++context.sequence;
            frame.renderFence = renderFence;
            frame.recording = false;
        }
        context.changed.notify_one();
    }
CS_STRICT_SYSTEMS_END

    void details::PresentationState::runOutput() noexcept
    {
        profiler::setThreadName("Present");
        try
        {
            while (true)
            {
                {
                    std::unique_lock lock(mutex);
                    if (stopping) break;
                    if (resizePending) {
                        const auto w = requestedWidth,h = requestedHeight;
                        lock.unlock(); resizeTargets(w,h); lock.lock();
                        resizePending = false; changed.notify_all();
                    }
                }
                pollOutput();
                // New frames wake this early. The timeout also retires GPU
                // copies and checks DXGI readiness without spinning.
                std::unique_lock lock(mutex);
                changed.wait_for(lock, std::chrono::milliseconds(1));
                if (stopping) break;
            }
        }
        catch (...)
        {
            std::lock_guard lock(mutex);
            outputError = std::current_exception();
            changed.notify_all();
        }
    }

    void details::PresentationState::pollOutput()
    {
        auto& context = *this;
        {
            std::lock_guard lock(context.mutex);
            for (auto& frame : context.frames)
                if (frame.copying && context.presentationQueue.isComplete(frame.copyFence))
                    frame.copying = false;
        }
        auto presentPending = [&] {
            if (!context.pendingPresent) return true;
            CS_PROFILE_SCOPE("Present");
            const auto result = context.swapChain.present(0, DXGI_PRESENT_DO_NOT_WAIT);
            if (result == DXGI_ERROR_WAS_STILL_DRAWING) return false;
            require(SUCCEEDED(result), "Mailbox Present failed");
            context.pendingPresent = false;
            bool resumed;
            { std::lock_guard lock(context.mutex); resumed = context.lastPresented == 0; context.lastPresented = context.lastCopied; }
            if (resumed) log::debug("Presentation resumed at {}x{}, frame {}",context.width,context.height,context.lastCopied);
            context.changed.notify_all();
            return true;
        };
        if (!presentPending()) return;
        // Keep at most one color texture owned by the copy queue.
        if (!context.presentationQueue.isComplete(context.lastCopyFence)) return;
        // Present(DO_NOT_WAIT) provides backpressure. A consumed latency event
        // can otherwise strand output after resize/occlusion without a Present
        // to signal it again. Queue fences still protect every copy allocator.
        details::PresentationState::Frame* newest = nullptr;
        {
            std::lock_guard lock(context.mutex);
            for (auto& frame : context.frames)
            {
                if (!frame.recording && !frame.copying && frame.sequence > context.lastCopied
                    && context.graphics->isComplete(frame.renderFence)
                    && (!newest || frame.sequence > newest->sequence))
                    newest = &frame;
            }
            if (!newest) return;
            newest->copying = true;
        }
        const auto index = context.swapChain.backBufferIndex();
        auto& copy = context.copies[index];
        if (!context.presentationQueue.isComplete(copy.lastFenceValue()))
        {
            std::lock_guard lock(context.mutex);
            newest->copying = false;
            return;
        }
        CS_PROFILE_SCOPE("Output copy");
        copy.reset();
        auto* destination = context.swapChain.currentBackBuffer();
        transition(copy, destination, D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_COPY_DEST);
        // Offscreen colors stay in COPY_SOURCE between draws on both direct queues.
        copy.raw()->CopyResource(destination, newest->color.Get());
        transition(copy, destination, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_PRESENT);
        copy.close();
        context.presentationQueue.addCommands(&copy);
        context.presentationQueue.execute();
        {
            std::lock_guard lock(context.mutex);
            newest->copyFence = copy.lastFenceValue();
            context.lastCopied = newest->sequence;
        }
        context.lastCopyFence = copy.lastFenceValue();
        context.pendingPresent = true;
        presentPending();
    }

}
