#pragma once
#include "renderer.h"
#include "swap_chain.h"
#include "render_queue.h"
#include "descriptor_allocation.h"
#include <mutex>
#include <thread>
#include <condition_variable>
#include <exception>

namespace csyren::core { class Window; }

namespace csyren::render::details
{
    class FrameGraphBackend;
    // Backend implementation detail, owned jointly by the two frame systems.
    struct PresentationState
    {
        static constexpr UINT FrameCount = Renderer::FramesInFlight;
        struct Frame
        {
            ComPtr<ID3D12Resource> color;
            DescriptorAllocation rtv;
            UINT64 sequence{ 0 };
            UINT64 renderFence{ 0 };
            UINT64 copyFence{ 0 };
            float clearColor[4]{};
            bool recording{ false };
            bool copying{ false };
        };

        ~PresentationState() { shutdown(); }
        void init();
        void init(ID3D12Device* device,RenderQueue& graphics,DescriptorManager& descriptors,core::Window& window);
        void shutdown();
        void prepareTarget(Frame& frame, const float color[4]);
        void resize(UINT width, UINT height);
        void resizeTargets(UINT width, UINT height);
        void prepareDepth();
        void runOutput() noexcept;
        void pollOutput();

        std::shared_ptr<FrameGraphBackend> graph;
        float background[4]{0.1f, 0.1f, 0.3f, 1.0f};
        std::mutex mutex;
        std::condition_variable changed;
        std::thread outputThread;
        bool stopping{ false };
        bool resizePending{ false };
        UINT requestedWidth{0},requestedHeight{0};
        std::exception_ptr outputError;

        SwapChain swapChain;
        RenderQueue presentationQueue;
        Frame frames[FrameCount];
        CommandList commands[Renderer::FramesInFlight];
        UINT commandIndex{ Renderer::FramesInFlight - 1 };
        CommandList copies[2];
        ComPtr<ID3D12Resource> depth;
        DescriptorAllocation dsv;
        UINT frameIndex{ FrameCount - 1 };
        UINT64 sequence{ 0 };
        UINT64 lastCopied{ 0 };
        UINT64 lastPresented{ 0 };
        UINT64 lastCopyFence{ 0 };
        UINT width{ 0 }, height{ 0 };
        bool pendingPresent{ false };
        bool initialized{ false };
        ComPtr<ID3D12Device> device;
        RenderQueue* graphics = nullptr;
        DescriptorManager* descriptors = nullptr;
    };
}
