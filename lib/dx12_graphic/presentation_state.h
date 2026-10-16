#pragma once
#include "renderer.h"
#include "swap_chain.h"
#include "render_queue.h"
#include "descriptor_allocation.h"
#include <mutex>
#include <thread>
#include <condition_variable>
#include <exception>

namespace csyren::render::details
{
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
        void shutdown();
        void prepareTarget(Frame& frame, const float color[4]);
        void runOutput() noexcept;
        void pollOutput();

        std::mutex mutex;
        std::condition_variable changed;
        std::thread outputThread;
        bool stopping{ false };
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
        UINT64 lastCopyFence{ 0 };
        UINT width{ 0 }, height{ 0 };
        bool pendingPresent{ false };
        bool outputReady{ false };
        bool initialized{ false };
    };
}
