#include "pch.h"
#include "presentation_system.h"
#include "frame_prepare_system.h"
#include "presentation_state.h"
#include "frame_graph_backend.h"
#include "core/system_manager.h"
#include "resource_manager.h"
#include "core/scene.h"
#include "core/time.h"
#include "core/camera.h"
#include "core/transform.h"
#include "math/matrix4x4.h"
#include <stdexcept>
#include <algorithm>
#include <chrono>

namespace csyren::render
{
    core::TaskDescription FrameAcquireSystem::describe() const {
        core::TaskDescription d; d.phase = "acquire"; d.affinity = core::TaskAffinity::MainThread;
        d.read<core::Camera>().read<core::Transform>().readResource<core::Time>();
        d.writeResource<Renderer>().writeResource<ResourceManager>().writeResource<DescriptorManager>();
        d.writeResource<details::FrameRecording>({}, "ready"); return d;
    }
    core::TaskDescription FramePrepareSystem::describe() const {
        core::TaskDescription d; d.phase = "prepare";
        d.gpu("frame.color", D3D12_RESOURCE_STATE_RENDER_TARGET, core::AccessMode::Write);
        d.gpu("frame.depth", D3D12_RESOURCE_STATE_DEPTH_WRITE, core::AccessMode::Write);
        return d;
    }
    core::TaskDescription PresentationSystem::describe() const {
        core::TaskDescription d; d.phase = "submit"; d.affinity = core::TaskAffinity::MainThread;
        d.writeResource<Renderer>().writeResource<details::FrameRecording>("ready", "submitted"); return d;
    }

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
        if (!_state->graph) _state->graph = std::make_shared<details::FrameGraphBackend>(*_state);
        core::Services::get<core::SystemManager>()->taskBackend(_state->graph);
    }
    void FrameAcquireSystem::shutdown() { _state.reset(); }
    void FramePrepareSystem::init() { _state = acquireState(); }
    void PresentationSystem::init() { _state = acquireState(); }
    void FramePrepareSystem::shutdown() { _state.reset(); }
    void PresentationSystem::shutdown() { _state.reset(); }

    void FrameAcquireSystem::updateCamera(float (&background)[4])
    {
        auto* renderer = core::Services::get<Renderer>();
        auto* variables = renderer->getEngineVariableBuffer();
        const auto time = core::Services::get<const core::Time>()->totalTime();
        variables->totalTime = time;
        for (auto [id, camera, transform] : core::Services::get<core::Scene>()->view<const core::Camera, const core::Transform>())
        {
            std::copy(&camera.background.x, &camera.background.x + 4, background);
            using namespace DirectX;
            auto invView = transform.world();
            auto view = XMMatrixInverse(nullptr, invView);
            auto projection = camera.projection == core::ProjectionType::Perspective
                ? math::Matrix4x4::perspective(camera.fov, camera.aspectRatio, camera.near, camera.far)
                : XMMatrixOrthographicLH(camera.fov * camera.aspectRatio, camera.fov, camera.near, camera.far);
            XMStoreFloat4x4(&variables->invViewMatrix, invView);
            XMStoreFloat4x4(&variables->viewMatrix, view);
            XMStoreFloat4x4(&variables->projectionMatrix, projection);
            XMStoreFloat4x4(&variables->viewProjectionMatrix, XMMatrixMultiply(view, projection));
            auto light = XMVector3TransformNormal(XMVectorSet(0.3f, -1.0f, 0.3f, 0.0f),
                                                 XMMatrixRotationZ(time * XM_2PI / 10.0f));
            XMStoreFloat4(&variables->lightDirection, XMVector3Normalize(light));
            return;
        }
    }

    void FrameAcquireSystem::execute()
    {
        auto& context = *_state;
        auto* renderer = core::Services::get<Renderer>();
        context.commandIndex = (context.commandIndex + 1) % Renderer::FramesInFlight;
        context.graph->acquire();
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
        updateCamera(background);
        // Stable colors reuse the texture without waiting for its previous draw:
        // both writes are already ordered on the same GPU queue.
        if (frame.color && !std::equal(background, background + 4, frame.clearColor))
            renderer->queue().syncAwaitComplete(frame.renderFence);
        context.prepareTarget(frame, background);
        std::copy(background, background + 4, context.background);
        core::Services::get<ResourceManager>()->update();
    }

    void FramePrepareSystem::execute() {
        auto* commands = core::Services::get<Renderer>()->commandList();
        commands->ClearDepthStencilView(_state->dsv.cpu, D3D12_CLEAR_FLAG_DEPTH, 0.0f, 0, 0, nullptr);
        commands->ClearRenderTargetView(_state->frames[_state->frameIndex].rtv.cpu, _state->background, 0, nullptr);
    }

    void PresentationSystem::execute()
    {
        auto& context = *_state;
        auto* renderer = core::Services::get<Renderer>();
        auto& frame = context.frames[context.frameIndex];
        const auto renderFence = context.graph->submit();
        {
            std::lock_guard lock(context.mutex);
            frame.sequence = ++context.sequence;
            frame.renderFence = renderFence;
            frame.recording = false;
        }
        context.changed.notify_one();
    }

    void details::PresentationState::runOutput() noexcept
    {
        try
        {
            while (true)
            {
                {
                    std::unique_lock lock(mutex);
                    if (stopping) break;
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
            const auto result = context.swapChain.present(0, DXGI_PRESENT_DO_NOT_WAIT);
            if (result == DXGI_ERROR_WAS_STILL_DRAWING) return false;
            require(SUCCEEDED(result), "Mailbox Present failed");
            context.pendingPresent = false;
            context.outputReady = false;
            return true;
        };
        if (!presentPending()) return;
        // Keep at most one color texture owned by the copy queue.
        if (!context.presentationQueue.isComplete(context.lastCopyFence)) return;
        if (!context.outputReady) context.outputReady = context.swapChain.ready();
        if (!context.outputReady) return;
        auto* renderer = core::Services::get<Renderer>();
        details::PresentationState::Frame* newest = nullptr;
        {
            std::lock_guard lock(context.mutex);
            for (auto& frame : context.frames)
            {
                if (!frame.recording && !frame.copying && frame.sequence > context.lastCopied
                    && renderer->queue().isComplete(frame.renderFence)
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
