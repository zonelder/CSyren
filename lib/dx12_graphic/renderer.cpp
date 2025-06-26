#include "pch.h"
#include "renderer.h"

#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "d3d12.lib")

using Microsoft::WRL::ComPtr;

namespace csyren::render
{
    Renderer::~Renderer()
    {
        if (_device)
        {
            waitForGpu();
            if (_fenceEvent)
            {
                CloseHandle(_fenceEvent);
                _fenceEvent = nullptr;
            }
        }
    }

    bool Renderer::init(HWND hwnd, UINT width, UINT height)
    {
        UINT factoryFlags = 0;
        ComPtr<IDXGIFactory4> factory;
        if (FAILED(CreateDXGIFactory2(factoryFlags, IID_PPV_ARGS(&factory))))
            return false;

        if (FAILED(D3D12CreateDevice(nullptr, D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&_device))))
            return false;

        D3D12_COMMAND_QUEUE_DESC queueDesc = {};
        queueDesc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
        queueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
        if (FAILED(_device->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&_commandQueue))))
            return false;

        DXGI_SWAP_CHAIN_DESC1 swapDesc = {};
        swapDesc.BufferCount = FrameCount;
        swapDesc.Width = width;
        swapDesc.Height = height;
        swapDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        swapDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        swapDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
        swapDesc.SampleDesc.Count = 1;

        ComPtr<IDXGISwapChain1> swapChain1;
        if (FAILED(factory->CreateSwapChainForHwnd(_commandQueue.Get(), hwnd, &swapDesc, nullptr, nullptr, &swapChain1)))
            return false;

        if (FAILED(factory->MakeWindowAssociation(hwnd, DXGI_MWA_NO_ALT_ENTER)))
            return false;

        if (FAILED(swapChain1.As(&_swapChain)))
            return false;

        _frameIndex = _swapChain->GetCurrentBackBufferIndex();

        D3D12_DESCRIPTOR_HEAP_DESC rtvHeapDesc = {};
        rtvHeapDesc.NumDescriptors = FrameCount;
        rtvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
        rtvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
        if (FAILED(_device->CreateDescriptorHeap(&rtvHeapDesc, IID_PPV_ARGS(&_rtvHeap))))
            return false;

        _rtvDescriptorSize = _device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
        D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle = _rtvHeap->GetCPUDescriptorHandleForHeapStart();
        for (UINT i = 0; i < FrameCount; ++i)
        {
            if (FAILED(_swapChain->GetBuffer(i, IID_PPV_ARGS(&_renderTargets[i]))))
                return false;
            _device->CreateRenderTargetView(_renderTargets[i].Get(), nullptr, rtvHandle);
            rtvHandle.ptr += _rtvDescriptorSize;
        }

        if (FAILED(_device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&_commandAllocator))))
            return false;

        if (FAILED(_device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, _commandAllocator.Get(), nullptr, IID_PPV_ARGS(&_commandList))))
            return false;

        if (FAILED(_commandList->Close()))
            return false;

        if (FAILED(_device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&_fence))))
            return false;

        _fenceValue = 1;
        _fenceEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);
        if (!_fenceEvent)
            return false;

        _viewport = { 0.0f, 0.0f, static_cast<float>(width), static_cast<float>(height), 0.0f, 1.0f };

        return true;
    }

    void Renderer::beginFrame()
    {
        _commandAllocator->Reset();
        _commandList->Reset(_commandAllocator.Get(), nullptr);

        D3D12_RESOURCE_BARRIER barrier = {};
        barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barrier.Transition.pResource = _renderTargets[_frameIndex].Get();
        barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_PRESENT;
        barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
        barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        _commandList->ResourceBarrier(1, &barrier);

        D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle = _rtvHeap->GetCPUDescriptorHandleForHeapStart();
        rtvHandle.ptr += _frameIndex * _rtvDescriptorSize;
        _commandList->OMSetRenderTargets(1, &rtvHandle, FALSE, nullptr);
        _commandList->RSSetViewports(1, &_viewport);
    }

    void Renderer::clear(const FLOAT color[4])
    {
        D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle = _rtvHeap->GetCPUDescriptorHandleForHeapStart();
        rtvHandle.ptr += _frameIndex * _rtvDescriptorSize;
        _commandList->ClearRenderTargetView(rtvHandle, color, 0, nullptr);
    }

    void Renderer::endFrame()
    {
        D3D12_RESOURCE_BARRIER barrier = {};
        barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barrier.Transition.pResource = _renderTargets[_frameIndex].Get();
        barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
        barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PRESENT;
        barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        _commandList->ResourceBarrier(1, &barrier);

        _commandList->Close();

        ID3D12CommandList* cmds[] = { _commandList.Get() };
        _commandQueue->ExecuteCommandLists(1, cmds);

        _swapChain->Present(1, 0);

        const UINT64 fenceToWaitFor = _fenceValue;
        _commandQueue->Signal(_fence.Get(), fenceToWaitFor);
        _fenceValue++;

        if (_fence->GetCompletedValue() < fenceToWaitFor)
        {
            _fence->SetEventOnCompletion(fenceToWaitFor, _fenceEvent);
            WaitForSingleObject(_fenceEvent, INFINITE);
        }

        _frameIndex = _swapChain->GetCurrentBackBufferIndex();
    }

    void Renderer::waitForGpu()
    {
        const UINT64 fenceToWaitFor = _fenceValue;
        _commandQueue->Signal(_fence.Get(), fenceToWaitFor);
        _fenceValue++;

        _fence->SetEventOnCompletion(fenceToWaitFor, _fenceEvent);
        WaitForSingleObject(_fenceEvent, INFINITE);
    }

}