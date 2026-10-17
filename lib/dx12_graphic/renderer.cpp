#include "pch.h"
#include "resource_manager.h"
#include "texture.h"
#include "renderer.h"

using Microsoft::WRL::ComPtr;
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


        if (!_uploadCommands.init(_device.Get(), D3D12_COMMAND_LIST_TYPE_DIRECT))
            throw std::runtime_error("Failed to create upload command list");
    }

    Renderer::~Renderer()
    {
        if (_device)
        {
            waitForGpu();
        }
    }


    bool Renderer::earlyInit()
    {
        _ownerThread = std::this_thread::get_id();
        enableDebugLayer();
        createDevice();
        createCommandQueue();

        constexpr size_t MB = 1024 * 1024;
        constexpr size_t perEntitySize = 2* MB; // Size for world matrix + other per-object data for whole scene render.
        
        if (!_perEntityCB.init(_device.Get(), perEntitySize, FramesInFlight))
        {
            return false;
        }
        _perEntityCB.beginFrame(0);
        details::EngineUpdateRegistry::instance().initialize();
        details::EngineSemanticRegistry::instance().initialize();

        return true;
    }

    Renderer::RecordingData* Renderer::recording() const {
        if (_recording && _recording->renderer == this) return _recording->data;
        if (std::this_thread::get_id() != _ownerThread)
            throw std::logic_error("GPU commands require a declared GPU system task");
        return nullptr;
    }
    ID3D12GraphicsCommandList* Renderer::commandList() const {
        auto* data = recording(); return data ? data->commands.raw() : _uploadCommands.raw();
    }
    UploadRingBuffer* Renderer::getUploadBuffer() {
        auto* data = recording(); return data ? &data->upload : &_perEntityCB;
    }
    EngineVariableBuffer* Renderer::getEngineVariableBuffer() {
        auto* data = recording(); return data ? &data->engine : &_engineVariableBuffer;
    }
    EntityVariableBuffer* Renderer::getEntityVariableBuffer() {
        auto* data = recording(); return data ? &data->entity : &_entityVariableBuffer;
    }

    void Renderer::waitForGpu()
    {
        _mainQueue.signal();
        _mainQueue.syncAwaitLastComplete();
    }

    //TODO CHANGE API
    bool Renderer::bindMaterial(const ResourceManager& rm, MaterialHandle mathandle,const VertexLayout& vertexLayout)
    {
        auto* data = recording();
        CommandList& cmdList = data ? data->commands : _uploadCommands;
        auto* material = rm.getMaterial(mathandle);
        if (!material)
            return false;

        auto shaderHandle = material->getShader();
        auto* shader = rm.getShader(material->getShader());
        if (!shader)
            return false;

        auto pso = core::Services::get < details::PSOFactory > ()->get(shaderHandle, shader, material->getStates(), vertexLayout);
        cmdList.setPipelineState(pso);
        cmdList.setRootSignature(shader->getRootSignature());

        auto& binder = data ? data->binder : _parameterBinder;
        auto& upload = data ? data->upload : _perEntityCB;
        if (!binder.updateMaterialBuffer(_device.Get(), cmdList.raw(), &rm, upload, mathandle))
        {
            return false;
        }

        auto& textures = material->textures();
        for (const auto& [name, handle] : textures)
        {
            const Texture* tex = rm.getTexture(handle);
            if (!tex || tex->status() != LoadStatus::Loaded) continue;

            auto gpuHandle = tex->getGpuSrvHandle();
            // assume rootParameterIndex is known by shader reflection
            UINT rootIndex = shader->getRootParameterIndex(name);
            cmdList.setGraphicsRootDescriptorTable(rootIndex, gpuHandle);
        }

        if (!binder.updateFrameBuffer(cmdList.raw(), *getEngineVariableBuffer(), shader->getSemanticBuffer(details::CBufferUpdateType::Pass), upload))
        {
            return false;
        }



        return true;
    }

    bool Renderer::bindEntity(const SemanticBufferLayout* layout)
    {
        auto* data = recording();
        CommandList& cmdList = data ? data->commands : _uploadCommands;
        auto& binder = data ? data->binder : _parameterBinder;
        return binder.updateEntityBuffer(cmdList.raw(), *getEntityVariableBuffer(), layout, *getUploadBuffer());
    }


    void Renderer::beginResourceUpload()
    {
        auto* data = recording();
        CommandList& cmdList = data ? data->commands : _uploadCommands;
        cmdList.reset();
    }


    void Renderer::endResourceUpload()
    {
        auto* data = recording();
        CommandList& cmdList = data ? data->commands : _uploadCommands;
        cmdList.close();
        _mainQueue.addCommands(&cmdList);
        _mainQueue.execute();
        waitForGpu();
    }

}