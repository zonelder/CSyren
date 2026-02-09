#include "pch.h"
#include "resource_manager.h"
#include "texture.h"
#include "renderer.h"
#include <d3dx12.h>


using Microsoft::WRL::ComPtr;
#define DEBUG_RENDER = defined(_DEBUG) && (_WIN32_WINNT >= 0x0603);
namespace csyren::render
{
    void Renderer::enableDebugLayer()
    {
#if defined(_DEBUG)
        details::ComPtr<ID3D12Debug> debug;
        if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debug))))
        {
            debug->EnableDebugLayer();
        }

        ComPtr<ID3D12Debug1> debug1;
        if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debug1))))
        {
            debug1->SetEnableGPUBasedValidation(TRUE);
            debug1->SetEnableSynchronizedCommandQueueValidation(TRUE);
        }
#endif
    }

    void Renderer::createFactory()
    {
        UINT flags = 0;
        static_assert(_WIN32_WINNT >= 0x0A00);//TODO to be documented.
#if defined(_DEBUG)
        flags |= DXGI_CREATE_FACTORY_DEBUG;
#endif
        if (DX_FAILED(CreateDXGIFactory2(flags, IID_PPV_ARGS(&_factory))))
            throw std::runtime_error("Failed to create DXGI factory");
    }
    void Renderer::createDevice()
    {
        if (DX_FAILED(D3D12CreateDevice(
            nullptr,
            D3D_FEATURE_LEVEL_11_0,
            IID_PPV_ARGS(&_device))))
        {
            throw std::runtime_error("Failed to create D3D12 device");
        }
    }
    void Renderer::createCommandQueue()
    {
        D3D12_COMMAND_QUEUE_DESC desc = {};
        desc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;

        if (DX_FAILED(_device->CreateCommandQueue(&desc, IID_PPV_ARGS(&_commandQueue))))
            throw std::runtime_error("Failed to create command queue");
    }

    void Renderer::createSwapChain(HWND hwnd, UINT width, UINT height)
    {
        DXGI_SWAP_CHAIN_DESC1 desc = {};
        desc.BufferCount = FrameCount;
        desc.Width = width;
        desc.Height = height;
        desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
        desc.SampleDesc.Count = 1;

        ComPtr<IDXGISwapChain1> sc1;
        if (FAILED(_factory->CreateSwapChainForHwnd(
            _commandQueue.Get(), hwnd, &desc, nullptr, nullptr, &sc1)))
        {
            throw std::runtime_error("Failed to create swapchain");
        }

        sc1.As(&_swapChain);
        _factory->MakeWindowAssociation(hwnd, DXGI_MWA_NO_ALT_ENTER);
        _frameIndex = _swapChain->GetCurrentBackBufferIndex();
    }

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
        enableDebugLayer();
        createFactory();
        createDevice();
        createCommandQueue();
        createSwapChain(hwnd, width, height);

        D3D12_DESCRIPTOR_HEAP_DESC rtvHeapDesc = {};
        rtvHeapDesc.NumDescriptors = FrameCount;
        rtvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
        rtvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
        if (DX_FAILED(_device->CreateDescriptorHeap(&rtvHeapDesc, IID_PPV_ARGS(&_rtvHeap))))
            return false;

        _rtvDescriptorSize = _device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
        D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle = _rtvHeap->GetCPUDescriptorHandleForHeapStart();
        for (UINT i = 0; i < FrameCount; ++i)
        {
            if (DX_FAILED(_swapChain->GetBuffer(i, IID_PPV_ARGS(&_renderTargets[i]))))
                return false;
            _device->CreateRenderTargetView(_renderTargets[i].Get(), nullptr, rtvHandle);
            rtvHandle.ptr += _rtvDescriptorSize;
        }

        D3D12_DESCRIPTOR_HEAP_DESC dsvHeapDesc = {};
        dsvHeapDesc.NumDescriptors = 1;
        dsvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
        dsvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;

        if (DX_FAILED(_device->CreateDescriptorHeap(&dsvHeapDesc, IID_PPV_ARGS(&_dsvHeap))))
            return false;

        _dsvDescriptorSize = _device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_DSV);

        D3D12_RESOURCE_DESC depthDesc = {};
        depthDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        depthDesc.Width = width;
        depthDesc.Height = height;
        depthDesc.DepthOrArraySize = 1;
        depthDesc.MipLevels = 1;
        depthDesc.Format = DXGI_FORMAT_D32_FLOAT;
        depthDesc.SampleDesc.Count = 1;
        depthDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
        depthDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;

        D3D12_CLEAR_VALUE depthClearValue = {};
        depthClearValue.Format = DXGI_FORMAT_D32_FLOAT;
        depthClearValue.DepthStencil.Depth = 0.0f;
        depthClearValue.DepthStencil.Stencil = 0;

        D3D12_HEAP_PROPERTIES heapProps = {};
        heapProps.Type = D3D12_HEAP_TYPE_DEFAULT;

        if(DX_FAILED(_device->CreateCommittedResource(&heapProps,D3D12_HEAP_FLAG_NONE,&depthDesc,D3D12_RESOURCE_STATE_DEPTH_WRITE,&depthClearValue,IID_PPV_ARGS(&_depthStencil))))
            return false;

        _depthStencil->SetName(L"BackBufferDepthStencil");

        D3D12_DEPTH_STENCIL_VIEW_DESC dsvDesc = {};
        dsvDesc.Format = DXGI_FORMAT_D32_FLOAT;
        dsvDesc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;

        _device->CreateDepthStencilView(_depthStencil.Get(), &dsvDesc, _dsvHeap->GetCPUDescriptorHandleForHeapStart());

        if (DX_FAILED(_device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&_commandAllocator))))
            return false;

        if (DX_FAILED(_device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, _commandAllocator.Get(), nullptr, IID_PPV_ARGS(&_commandList))))
            return false;

        if (DX_FAILED(_commandList->Close()))
            return false;

        if (DX_FAILED(_device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&_fence))))
            return false;

        _fenceValue = 1;
        _fenceEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);
        if (!_fenceEvent)
            return false;

        _pSrvHeapManager = std::make_unique<DescriptorHeapManager>();
        if (!_pSrvHeapManager->init(_device.Get(), D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 1024, true))
        {
            return false;
        }


        constexpr size_t MB = 1024 * 1024;
        constexpr size_t perEntitySize = 2* MB; // Size for world matrix + other per-object data for whole scene render.
        
        if (!_perEntityCB.init(_device.Get(), perEntitySize))
        {
            return false;
        }

        _pPSOFactory = std::make_unique<details::PSOFactory>(_device.Get());

        _viewport = { 0.0f, 0.0f, static_cast<float>(width), static_cast<float>(height), 0.0f, 1.0f };
        _scissor = { 0, 0, static_cast<LONG>(width), static_cast<LONG>(height) };
        details::EngineUpdateRegistry::instance().initialize();
        details::EngineSemanticRegistry::instance().initialize();

        return true;
    }

    void Renderer::beginFrame()
    {
        _commandAllocator->Reset();
        _commandList->Reset(_commandAllocator.Get(), nullptr);
        _perEntityCB.beginFrame();
        // --- Barrier для swapchain ---
        D3D12_RESOURCE_BARRIER rtvBarrier = {};
        rtvBarrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        rtvBarrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
        rtvBarrier.Transition.pResource = _renderTargets[_frameIndex].Get();
        rtvBarrier.Transition.StateBefore = D3D12_RESOURCE_STATE_PRESENT;
        rtvBarrier.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
        rtvBarrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        _commandList->ResourceBarrier(1, &rtvBarrier);

        // --- Barrier для depth ---
        if (_depthStencilCurrentState != D3D12_RESOURCE_STATE_DEPTH_WRITE)
        {
            D3D12_RESOURCE_BARRIER depthBarrier = {};
            depthBarrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
            depthBarrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
            depthBarrier.Transition.pResource = _depthStencil.Get();
            depthBarrier.Transition.StateBefore = _depthStencilCurrentState;
            depthBarrier.Transition.StateAfter = D3D12_RESOURCE_STATE_DEPTH_WRITE;
            depthBarrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
            _commandList->ResourceBarrier(1, &depthBarrier);
            _depthStencilCurrentState = D3D12_RESOURCE_STATE_DEPTH_WRITE;
        }

        // --- Bind render targets ---
        D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle = _rtvHeap->GetCPUDescriptorHandleForHeapStart();
        rtvHandle.ptr += _frameIndex * _rtvDescriptorSize;

        D3D12_CPU_DESCRIPTOR_HANDLE dsvHandle = _dsvHeap->GetCPUDescriptorHandleForHeapStart();
        _commandList->OMSetRenderTargets(1, &rtvHandle, FALSE, &dsvHandle);

        // --- Clear depth buffer ---
        _commandList->ClearDepthStencilView(dsvHandle, D3D12_CLEAR_FLAG_DEPTH, 0.0f, 0, 0, nullptr);

        // --- Viewport & scissor ---
        _commandList->RSSetViewports(1, &_viewport);
        _commandList->RSSetScissorRects(1, &_scissor);

        // --- Descriptor heap ---
        auto heap = _pSrvHeapManager->getHeap();
        _commandList->SetDescriptorHeaps(1u, &heap);
    }

    void Renderer::clear(const FLOAT color[4])
    {
        D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle = _rtvHeap->GetCPUDescriptorHandleForHeapStart();
        rtvHandle.ptr += _frameIndex * _rtvDescriptorSize;
        _commandList->ClearRenderTargetView(rtvHandle, color, 0, nullptr);
    }

    void Renderer::endFrame()
    {
        D3D12_RESOURCE_BARRIER rtvBarrier = {};
        rtvBarrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        rtvBarrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
        rtvBarrier.Transition.pResource = _renderTargets[_frameIndex].Get();
        rtvBarrier.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
        rtvBarrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PRESENT;
        rtvBarrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        _commandList->ResourceBarrier(1, &rtvBarrier);

        // --- Barrier для depth перед следующим кадром ---
        if (_depthStencilCurrentState != D3D12_RESOURCE_STATE_COMMON)
        {
            D3D12_RESOURCE_BARRIER depthBarrier = {};
            depthBarrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
            depthBarrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
            depthBarrier.Transition.pResource = _depthStencil.Get();
            depthBarrier.Transition.StateBefore = _depthStencilCurrentState;
            depthBarrier.Transition.StateAfter = D3D12_RESOURCE_STATE_COMMON;
            depthBarrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
            _commandList->ResourceBarrier(1, &depthBarrier);
            _depthStencilCurrentState = D3D12_RESOURCE_STATE_COMMON;
        }

        // --- Execute command list ---
        _commandList->Close();
        ID3D12CommandList* cmds[] = { _commandList.Get() };
        _commandQueue->ExecuteCommandLists(1, cmds);

        // --- Present ---
        _swapChain->Present(static_cast<UINT>(_enableVSync), 0);

        // --- GPU sync ---
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

    HRESULT Renderer::uploadTextureData(ID3D12Resource* destResource, const DirectX::ScratchImage& scratchImage)
    {
        const auto& metadata = scratchImage.GetMetadata();
        D3D12_RESOURCE_DESC uploadDesc = CD3DX12_RESOURCE_DESC::Buffer(GetRequiredIntermediateSize(destResource, 0, static_cast<UINT>(metadata.mipLevels)));
        Microsoft::WRL::ComPtr<ID3D12Resource> uploadBuffer;
        auto heapProp = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD);
        HRESULT hr = _device->CreateCommittedResource(
            &heapProp,
            D3D12_HEAP_FLAG_NONE,
            &uploadDesc,
            D3D12_RESOURCE_STATE_GENERIC_READ,
            nullptr,
            IID_PPV_ARGS(&uploadBuffer)
        );
        if (FAILED(hr)) return hr;
        uploadBuffer->SetName(L"TextureUploadBuffer");

        std::vector<D3D12_SUBRESOURCE_DATA> subresources;

        _commandAllocator->Reset();
        _commandList->Reset(_commandAllocator.Get(), nullptr);
        
        auto commonBarrier = CD3DX12_RESOURCE_BARRIER::Transition(destResource, D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_COPY_DEST);
        _commandList->ResourceBarrier(1, &commonBarrier);
        UpdateSubresources(_commandList.Get(), destResource, uploadBuffer.Get(), 0, 0, static_cast<UINT>(subresources.size()), subresources.data());
        auto copyBarrier = CD3DX12_RESOURCE_BARRIER::Transition(destResource, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
        _commandList->ResourceBarrier(1, &copyBarrier);


        hr = _commandList->Close();
        if (FAILED(hr)) return hr;

        ID3D12CommandList* ppCommandLists[] = { _commandList.Get() };
        _commandQueue->ExecuteCommandLists(_countof(ppCommandLists), ppCommandLists);

        waitForGpu();

        return S_OK;
    }

    //TODO CHANGE API
    bool Renderer::bindMaterial(ResourceManager& rm, MaterialHandle mathandle,const VertexLayout& vertexLayout)
    {
        auto* material = rm.getMaterial(mathandle);
        if (!material)
            return false;

        auto shaderHandle = material->getShader();
        auto* shader = rm.getShader(material->getShader());
        if (!shader)
            return false;

        auto pso = _pPSOFactory->get(shaderHandle, shader, material->getStates(), vertexLayout);
        _commandList->SetPipelineState(pso);
        _commandList->SetGraphicsRootSignature(shader->getRootSignature());

        if (!_parameterBinder.updateMaterialBuffer(_device.Get(), _commandList.Get(), &rm, _perEntityCB, mathandle))
        {
            return false;
        }

        auto& textures = material->textures();
        for (const auto& [name, handle] : textures)
        {
            Texture* tex = rm.getTexture(handle);
            if (!tex || tex->status() != LoadStatus::Loaded) continue;

            auto gpuHandle = tex->getGpuSrvHandle();
            // assume rootParameterIndex is known by shader reflection
            UINT rootIndex = shader->getRootParameterIndex(name);
            _commandList->SetGraphicsRootDescriptorTable(rootIndex, gpuHandle);
        }

        if (!_parameterBinder.updateFrameBuffer(_commandList.Get(), _engineVariableBuffer, shader->getSemanticBuffer(details::CBufferUpdateType::Pass), _perEntityCB))
        {
            return false;
        }



        return true;
    }

    bool Renderer::bindEntity(const SemanticBufferLayout* layout)
    {
        return _parameterBinder.updateEntityBuffer(_commandList.Get(), _entityVariableBuffer, layout, _perEntityCB);
    }


    void Renderer::beginResourceUpload()
    {
        DX_LOG(_commandAllocator->Reset());
        DX_LOG(_commandList->Reset(_commandAllocator.Get(), nullptr));
    }


    void Renderer::endResourceUpload()
    {
        DX_LOG(_commandList->Close());

        ID3D12CommandList* ppCommandLists[] = { _commandList.Get() };
        _commandQueue->ExecuteCommandLists(_countof(ppCommandLists), ppCommandLists);
        waitForGpu();
    }

}