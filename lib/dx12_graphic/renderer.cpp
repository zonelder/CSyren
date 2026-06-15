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
        if(!_mainQueue.init(_device.Get(), desc))
            throw std::runtime_error("Failed to create command queue");


        for (auto& cmdList : _cmdLists)
        {
            if (!cmdList.init(_device.Get(), D3D12_COMMAND_LIST_TYPE_DIRECT))
            {
                throw std::runtime_error("Failed to create command list");
            }
        }
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
            _mainQueue.raw(), hwnd, &desc, nullptr, nullptr, &sc1)))
        {
            throw std::runtime_error("Failed to create swapchain");
        }

        sc1.As(&_swapChain);
        _factory->MakeWindowAssociation(hwnd, DXGI_MWA_NO_ALT_ENTER);
    }

    Renderer::~Renderer()
    {
        if (_device)
        {
            waitForGpu();
        }
    }


    bool Renderer::earlyInit(HWND hwnd, UINT width, UINT height)
    {
        enableDebugLayer();
        createFactory();
        createDevice();
        createCommandQueue();
        createSwapChain(hwnd, width, height);

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


        constexpr size_t MB = 1024 * 1024;
        constexpr size_t perEntitySize = 2* MB; // Size for world matrix + other per-object data for whole scene render.
        
        if (!_perEntityCB.init(_device.Get(), perEntitySize))
        {
            return false;
        }
        _width = width;
        _height = height;
        details::EngineUpdateRegistry::instance().initialize();
        details::EngineSemanticRegistry::instance().initialize();

        return true;
    }

    void Renderer::init()
    {
        auto& mgr = DescriptorManager::instance();
        for (UINT i = 0; i < FrameCount; ++i)
        {
            if (DX_FAILED(_swapChain->GetBuffer(i, IID_PPV_ARGS(&_renderTargets[i]))))
                return;
            _rtv[i] = mgr.createRTV(_renderTargets[i].Get(), nullptr);
        }

        D3D12_DEPTH_STENCIL_VIEW_DESC dsvDesc = {};
        dsvDesc.Format = DXGI_FORMAT_D32_FLOAT;
        dsvDesc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;

        _dsv = mgr.createDSV(_depthStencil.Get(), &dsvDesc);
    }

    void Renderer::beginFrame()
    {
        _frameIndex = (_frameIndex + 1 )% FrameCount;

        CommandList& cmdList = _cmdLists[_frameIndex];
        _mainQueue.syncAwaitComplete(cmdList.lastFenceValue());
        cmdList.reset();
        _perEntityCB.beginFrame();
        _lastBackBuffer = _swapChain->GetCurrentBackBufferIndex();
        // --- Barrier для swapchain ---
        D3D12_RESOURCE_BARRIER rtvBarrier = {};
        rtvBarrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        rtvBarrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
        rtvBarrier.Transition.pResource = _renderTargets[_lastBackBuffer].Get();
        rtvBarrier.Transition.StateBefore = D3D12_RESOURCE_STATE_PRESENT;
        rtvBarrier.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
        rtvBarrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        cmdList.resourceBarrier(rtvBarrier);

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
            cmdList.resourceBarrier(depthBarrier);
            _depthStencilCurrentState = D3D12_RESOURCE_STATE_DEPTH_WRITE;
        }


        cmdList.setRenderTarget(0, _rtv[_lastBackBuffer].cpu);
        cmdList.setDepthStencil( _dsv.cpu );
        cmdList.submitRenderTargets();

        cmdList.clearDepthStencil(0.0f,0);
        cmdList.setViewport({ 0.0f, 0.0f, static_cast<float>(_width), static_cast<float>(_height), 0.0f, 1.0f });

        cmdList.setScissorRect({ 0, 0, static_cast<LONG>(_width), static_cast<LONG>(_height) });
    }


    void Renderer::clear(const FLOAT color[4])
    {
        _cmdLists[_frameIndex].clearRenderTarget(0,color);
    }

    void Renderer::endFrame()
    {
        CommandList& cmdList = _cmdLists[_frameIndex];
        D3D12_RESOURCE_BARRIER rtvBarrier = {};
        rtvBarrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        rtvBarrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
        rtvBarrier.Transition.pResource = _renderTargets[_lastBackBuffer].Get();
        rtvBarrier.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
        rtvBarrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PRESENT;
        rtvBarrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        cmdList.resourceBarrier(rtvBarrier);

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
            cmdList.resourceBarrier(depthBarrier);
            _depthStencilCurrentState = D3D12_RESOURCE_STATE_COMMON;
        }
        cmdList.close();
        _mainQueue.addCommands(&cmdList);
        _mainQueue.execute();

        // --- Present ---
        _swapChain->Present(static_cast<UINT>(_enableVSync), 0);
    }

    void Renderer::waitForGpu()
    {
        _mainQueue.signal();
        _mainQueue.syncAwaitLastComplete();
    }

    //TODO CHANGE API
    bool Renderer::bindMaterial(ResourceManager& rm, MaterialHandle mathandle,const VertexLayout& vertexLayout)
    {
        CommandList& cmdList = _cmdLists[_frameIndex];
        auto* material = rm.getMaterial(mathandle);
        if (!material)
            return false;

        auto shaderHandle = material->getShader();
        auto* shader = rm.getShader(material->getShader());
        if (!shader)
            return false;

        auto pso = details::PSOFactory::instancePtr()->get(shaderHandle, shader, material->getStates(), vertexLayout);
        cmdList.setPipelineState(pso);
        cmdList.setRootSignature(shader->getRootSignature());

        if (!_parameterBinder.updateMaterialBuffer(_device.Get(), cmdList.raw(), &rm, _perEntityCB, mathandle))
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
            cmdList.setGraphicsRootDescriptorTable(rootIndex, gpuHandle);
        }

        if (!_parameterBinder.updateFrameBuffer(cmdList.raw(), _engineVariableBuffer, shader->getSemanticBuffer(details::CBufferUpdateType::Pass), _perEntityCB))
        {
            return false;
        }



        return true;
    }

    bool Renderer::bindEntity(const SemanticBufferLayout* layout)
    {
        CommandList& cmdList = _cmdLists[_frameIndex];
        return _parameterBinder.updateEntityBuffer(cmdList.raw(), _entityVariableBuffer, layout, _perEntityCB);
    }


    void Renderer::beginResourceUpload()
    {
        CommandList& cmdList = _cmdLists[_frameIndex];
        cmdList.reset();
    }


    void Renderer::endResourceUpload()
    {
        CommandList& cmdList = _cmdLists[_frameIndex];
        cmdList.close();
        _mainQueue.addCommands(&cmdList);
        _mainQueue.execute();
        waitForGpu();
    }

}