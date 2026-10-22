#include "pch.h"
#include "resource_manager.h"
#include "texture.h"
#include "renderer.h"
#include "camera_render_target.h"
#include "compute_shader.h"
#include "core/time.h"
#include "core/transform_hierarchy.h"

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
            flushDeferredReleases();
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
    Renderer::RecordingData& Renderer::requireRecording(const char* what) const {
        auto* data = recording();
        if (!data) throw std::logic_error(std::string(what) + " requires a recording GPU task");
        return *data;
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

    std::shared_ptr<details::CameraRenderView> Renderer::addCameraView(CameraSource source)
    {
        auto view = std::make_shared<details::CameraRenderView>();
        _externalCameras.push_back({std::move(source),view});
        return view;
    }
    void Renderer::removeCameraView(const std::shared_ptr<details::CameraRenderView>& view)
    {
        std::erase_if(_externalCameras,[&](const auto& external) { return external.view == view; });
        std::erase(_views,view);
    }
    void Renderer::prepareCameraViews(const CameraQuery& cameras,float time,UINT width,UINT height)
    {
        _worldTransforms.update(cameras);
        struct Source { const core::Camera* camera; math::Matrix4x4 world; std::shared_ptr<details::CameraRenderView> view; };
        std::vector<Source> sources; _views.clear(); _primaryView.reset();
        int priority = 0;
        _cameraViews.each(cameras,[&](auto id,const auto& camera,const auto& transform,auto view) {
            sources.push_back({&camera,_worldTransforms.matrix(id),view});
            if (!_primaryView || camera.priority > priority || (camera.priority == priority && view->id < _primaryView->id)) {
                priority = camera.priority; _primaryView = view;
            }
        });
        if (_presentCameraOutput && _primaryView) _primaryView->target->request(width,height);
        for (const auto& external : _externalCameras) {
            auto [camera,transform] = external.source();
            if (camera && transform) sources.push_back({camera,transform->localMatrix(),external.view});
        }
        for (auto& source : sources) {
            auto& view = *source.view;
            const bool due = view.schedule.due(source.camera->render_type,view.target->needsResize());
            // A view nobody sees records no passes; its frame constants stay current
            // (the editor picks with them), and a pending render waits until it is shown.
            const bool shown = view.shown && (source.view != _primaryView || _presentCameraOutput || _primaryShown);
            view.render = due && shown;
            view.hidden = due && !shown;
            view.ticket = view.schedule.ticket();
            view.target->prepare(*this);
            if (due) {
                view.background = source.camera->background;
                view.engine = details::cameraFrame(*source.camera,source.world,view.target->width(),view.target->height(),time,_environment);
            }
            _views.push_back(source.view);
        }
        std::sort(_views.begin(),_views.end(),[](const auto& a,const auto& b) { return a->id < b->id; });
        if (_primaryView) _engineVariableBuffer = _primaryView->engine;
    }
    void Renderer::deferRelease(std::shared_ptr<void> resource)
    { if (resource) _deferredReleases.push_back({std::move(resource)}); }

    void Renderer::completeFrame(uint64_t fence)
    {
        for (auto& retired : _deferredReleases) if (!retired.fence) retired.fence = fence;
        collectDeferredReleases();
    }
    void Renderer::collectDeferredReleases()
    {
        std::erase_if(_deferredReleases, [&](const auto& retired) {
            return retired.fence && _mainQueue.isComplete(retired.fence);
        });
    }
    void Renderer::flushDeferredReleases()
    {
        waitForGpu();
        _deferredReleases.clear();
    }
    void Renderer::clearCamera()
    {
        auto* data = recording();
        if (!data) throw std::logic_error("Camera clear requires a render pass");
        if (data->depthTarget.ptr) clearDepth();
        if (data->colorCount) clearColor(&data->background.x);
    }
    void Renderer::clearColor(const float color[4])
    {
        auto* data = recording();
        if (!data || !data->colorCount) throw std::logic_error("Clear requires a declared color target");
        for (UINT i = 0; i < data->colorCount; ++i)
            data->commands.raw()->ClearRenderTargetView(data->colorTargets[i],color,0,nullptr);
    }
    void Renderer::clearDepth()
    {
        auto* data = recording();
        if (!data || !data->depthTarget.ptr) throw std::logic_error("Clear requires a declared depth target");
        data->commands.raw()->ClearDepthStencilView(data->depthTarget,D3D12_CLEAR_FLAG_DEPTH,0,0,0,nullptr);
    }
    void Renderer::waitForGpu()
    {
        _mainQueue.signal();
        _mainQueue.syncAwaitLastComplete();
    }

    MaterialStateDesc Renderer::passStates(MaterialStateDesc states) const
    {
        auto* data = recording();
        if (!data) return states;
        states.numRenderTargets = data->colorCount;
        for (UINT i = 0; i < D3D12_SIMULTANEOUS_RENDER_TARGET_COUNT; ++i)
            states.rtvFormats[i] = i < data->colorCount ? data->colorFormats[i] : DXGI_FORMAT_UNKNOWN;
        states.dsvFormat = data->depthFormat;
        if (!data->depthTarget.ptr) { states.depthStencilState.DepthEnable = FALSE; states.depthStencilState.StencilEnable = FALSE; }
        return states;
    }

    const CameraRenderTarget* Renderer::currentTarget() const { return requireRecording("Camera attachments").target; }
    UINT Renderer::currentWidth() const { return requireRecording("Pass size").width; }
    UINT Renderer::currentHeight() const { return requireRecording("Pass size").height; }

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

        // The pass decides the attachment formats; the material only carries blend/raster/depth state.
        auto pso = core::Services::get < details::PSOFactory > ()->get(shaderHandle, shader, passStates(material->getStates()), vertexLayout);
        if (!pso) return false;
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
            if (rootIndex == UINT_MAX) continue;
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

    bool Renderer::bindShader(const ResourceManager& rm, ShaderHandle handle, const MaterialStateDesc& states, const VertexLayout& vertexLayout)
    {
        auto& data = requireRecording("bindShader");
        auto* shader = rm.getShader(handle);
        if (!shader) return false;
        auto pso = core::Services::get<details::PSOFactory>()->get(handle, shader, passStates(states), vertexLayout);
        if (!pso) return false;
        data.commands.setPipelineState(pso);
        data.commands.setRootSignature(shader->getRootSignature());
        return data.binder.updateFrameBuffer(data.commands.raw(), data.engine, shader->getSemanticBuffer(details::CBufferUpdateType::Pass), data.upload);
    }

    bool Renderer::bindComputeShader(const ResourceManager& rm, ComputeShaderHandle handle)
    {
        auto& data = requireRecording("bindComputeShader");
        auto* shader = rm.getComputeShader(handle);
        if (!shader) return false;
        auto pso = core::Services::get<details::PSOFactory>()->getCompute(handle, shader);
        if (!pso) return false;
        data.commands.raw()->SetComputeRootSignature(shader->getRootSignature());
        data.commands.setPipelineState(pso);
        return true;
    }

    bool Renderer::setShaderTexture(const ShaderBase& shader, const std::string& name, D3D12_GPU_DESCRIPTOR_HANDLE srv)
    {
        auto& data = requireRecording("setShaderTexture");
        const UINT slot = shader.getRootParameterIndex(name);
        if (slot == UINT_MAX || !srv.ptr) return false;
        if (shader.getType() == ShaderType::Compute) data.commands.raw()->SetComputeRootDescriptorTable(slot, srv);
        else data.commands.raw()->SetGraphicsRootDescriptorTable(slot, srv);
        return true;
    }

    bool Renderer::setShaderBuffer(const ShaderBase& shader, const std::string& name, D3D12_GPU_VIRTUAL_ADDRESS address)
    {
        auto& data = requireRecording("setShaderBuffer");
        const UINT slot = shader.getRootParameterIndex(name);
        if (slot == UINT_MAX || !address) return false;
        if (shader.getType() == ShaderType::Compute) data.commands.raw()->SetComputeRootShaderResourceView(slot, address);
        else data.commands.raw()->SetGraphicsRootShaderResourceView(slot, address);
        return true;
    }

    bool Renderer::setShaderRWBuffer(const ShaderBase& shader, const std::string& name, D3D12_GPU_VIRTUAL_ADDRESS address)
    {
        auto& data = requireRecording("setShaderRWBuffer");
        const UINT slot = shader.getRootParameterIndex(name);
        if (slot == UINT_MAX || !address) return false;
        if (shader.getType() == ShaderType::Compute) data.commands.raw()->SetComputeRootUnorderedAccessView(slot, address);
        else data.commands.raw()->SetGraphicsRootUnorderedAccessView(slot, address);
        return true;
    }

    bool Renderer::setShaderConstants(const ShaderBase& shader, const std::string& name, const void* constants, size_t size)
    {
        auto& data = requireRecording("setShaderConstants");
        const UINT slot = shader.getRootParameterIndex(name);
        if (slot == UINT_MAX) return false;
        D3D12_GPU_VIRTUAL_ADDRESS address = 0;
        if (data.upload.update(constants, size, &address) == size_t(-1)) return false;
        if (shader.getType() == ShaderType::Compute) data.commands.raw()->SetComputeRootConstantBufferView(slot, address);
        else data.commands.raw()->SetGraphicsRootConstantBufferView(slot, address);
        return true;
    }

    bool Renderer::allocateUpload(size_t size, void** cpu, D3D12_GPU_VIRTUAL_ADDRESS* gpu)
    {
        auto& data = requireRecording("allocateUpload");
        return data.upload.allocate(size, cpu, gpu) != size_t(-1);
    }

    void Renderer::drawFullscreen()
    {
        auto& data = requireRecording("drawFullscreen");
        auto* commands = data.commands.raw();
        commands->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        commands->IASetVertexBuffers(0, 0, nullptr);
        commands->IASetIndexBuffer(nullptr);
        commands->DrawInstanced(3, 1, 0, 0);
    }

    void Renderer::dispatch(UINT x, UINT y, UINT z)
    {
        auto& data = requireRecording("dispatch");
        data.commands.raw()->Dispatch(x, y, z);
    }


    void Renderer::beginResourceUpload()
    {
        auto* data = recording();
        CommandList& cmdList = data ? data->commands : _uploadCommands;
        cmdList.reset();
        if (!data) _uploadOpen = true;
    }


    void Renderer::endResourceUpload()
    {
        auto* data = recording();
        CommandList& cmdList = data ? data->commands : _uploadCommands;
        cmdList.close();
        _mainQueue.addCommands(&cmdList);
        _mainQueue.execute();
        waitForGpu();
        if (!data) _uploadOpen = false;
    }

}
