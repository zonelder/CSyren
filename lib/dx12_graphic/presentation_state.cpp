#include "pch.h"
#include "presentation_state.h"
#include "core/window.h"
#include <stdexcept>
#include <algorithm>

namespace csyren::render::details
{
    namespace
    {
        void require(bool success, const char* message)
        {
            if (!success) throw std::runtime_error(message);
        }

    }

    void PresentationState::init()
    {
        auto* renderer = core::Services::get<Renderer>();
        auto* device = renderer->device();
        auto* window = core::Services::get<core::Window>();
        auto* descriptors = core::Services::get<DescriptorManager>();
        width = window->width();
        height = window->height();

        D3D12_COMMAND_QUEUE_DESC queueDesc{};
        queueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
        require(presentationQueue.init(device, queueDesc), "Failed to create presentation queue");
        details::ComPtr<IDXGIFactory4> factory;
        require(SUCCEEDED(CreateDXGIFactory2(0, IID_PPV_ARGS(&factory))), "Failed to create DXGI factory");
        DXGI_SWAP_CHAIN_DESC1 swapDesc{};
        swapDesc.Width = width;
        swapDesc.Height = height;
        swapDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        swapDesc.BufferCount = 2;
        swapDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        swapDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
        swapDesc.SampleDesc.Count = 1;
        require(swapChain.init(factory.Get(), device, presentationQueue.raw(), window->hwnd(), swapDesc),
                "Failed to create mailbox swap chain");

        initialized = true;
        for (auto& command : commands)
            require(command.init(device, queueDesc.Type), "Failed to create frame commands");
        D3D12_HEAP_PROPERTIES heap{};
        heap.Type = D3D12_HEAP_TYPE_DEFAULT;
        D3D12_RESOURCE_DESC desc{};
        desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        desc.Width = width;
        desc.Height = height;
        desc.DepthOrArraySize = 1;
        desc.MipLevels = 1;
        desc.SampleDesc.Count = 1;
        desc.Format = DXGI_FORMAT_D32_FLOAT;
        desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;
        D3D12_CLEAR_VALUE clear{};
        clear.Format = desc.Format;
        clear.DepthStencil.Depth = 0.0f;
        require(SUCCEEDED(device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc,
                D3D12_RESOURCE_STATE_DEPTH_WRITE, &clear, IID_PPV_ARGS(&depth))),
                "Failed to create frame depth buffer");
        D3D12_DEPTH_STENCIL_VIEW_DESC dsvDesc{};
        dsvDesc.Format = desc.Format;
        dsvDesc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
        dsv = descriptors->createDSV(depth.Get(), &dsvDesc);
        require(dsv.valid(), "Failed to allocate frame DSV");
        for (auto& copy : copies)
            require(copy.init(device, queueDesc.Type), "Failed to create presentation commands");
        initialized = true;
        outputThread = std::thread([this] { runOutput(); });
    }

    void PresentationState::prepareTarget(Frame& frame, const float color[4])
    {
        if (frame.color && std::equal(color, color + 4, frame.clearColor)) return;
        auto* device = core::Services::get<Renderer>()->device();
        auto* descriptors = core::Services::get<DescriptorManager>();
        D3D12_HEAP_PROPERTIES heap{};
        heap.Type = D3D12_HEAP_TYPE_DEFAULT;
        D3D12_RESOURCE_DESC desc{};
        desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        desc.Width = width;
        desc.Height = height;
        desc.DepthOrArraySize = 1;
        desc.MipLevels = 1;
        desc.SampleDesc.Count = 1;
        desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
        D3D12_CLEAR_VALUE clear{};
        clear.Format = desc.Format;
        std::copy(color, color + 4, clear.Color);
        ComPtr<ID3D12Resource> target;
        require(SUCCEEDED(device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc,
                D3D12_RESOURCE_STATE_COPY_SOURCE, &clear, IID_PPV_ARGS(&target))),
                "Failed to create mailbox color buffer");
        if (frame.rtv.valid()) descriptors->recreateRTV(frame.rtv, target.Get());
        else frame.rtv = descriptors->createRTV(target.Get());
        require(frame.rtv.valid(), "Failed to allocate mailbox RTV");
        frame.color = std::move(target);
        std::copy(color, color + 4, frame.clearColor);
    }

    void PresentationState::shutdown()
    {
        if (!initialized) return;
        {
            std::lock_guard lock(mutex);
            stopping = true;
        }
        changed.notify_one();
        if (outputThread.joinable())
        {
            // DXGI may send window messages from Present. Keep the owning
            // thread responsive while waiting for the output worker to exit.
            HANDLE thread = outputThread.native_handle();
            while (MsgWaitForMultipleObjects(1, &thread, FALSE, INFINITE, QS_ALLINPUT) == WAIT_OBJECT_0 + 1)
            {
                MSG message{};
                while (PeekMessage(&message, nullptr, 0, 0, PM_REMOVE))
                {
                    if (message.message == WM_QUIT) continue;
                    TranslateMessage(&message);
                    DispatchMessage(&message);
                }
            }
            outputThread.join();
        }
        // Drain both queues before editor/resource descriptors are released.
        core::Services::get<Renderer>()->queue().syncAwaitLastComplete();
        presentationQueue.syncAwaitLastComplete();
        auto* descriptors = core::Services::get<DescriptorManager>();
        for (auto& frame : frames)
        {
            descriptors->freeRTV(frame.rtv);
            frame.color.Reset();
        }
        descriptors->freeDSV(dsv);
        depth.Reset();
        initialized = false;
    }
}
