#include "pch.h"
#include "resource_manager.h"
#include "texture.h"
#include "renderer.h"
#include "camera_render_target.h"
#include "core/time.h"

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
    void Renderer::prepareCameraViews(core::Scene& scene,UINT width,UINT height)
    {
        struct Source { const core::Camera* camera; const core::Transform* transform; std::shared_ptr<details::CameraRenderView> view; };
        std::vector<Source> sources; _views.clear(); _primaryView.reset();
        int priority = 0;
        _cameraViews.each(scene,[&](const auto& camera,const auto& transform,auto view) {
            sources.push_back({&camera,&transform,view});
            if (!_primaryView || camera.priority > priority || (camera.priority == priority && view->id < _primaryView->id)) {
                priority = camera.priority; _primaryView = view;
            }
        });
        if (_presentCameraOutput && _primaryView) _primaryView->target->request(width,height);
        for (const auto& external : _externalCameras) {
            auto [camera,transform] = external.source();
            if (camera && transform) sources.push_back({camera,transform,external.view});
        }
        const auto time = core::Services::get<const core::Time>()->totalTime();
        for (auto& source : sources) {
            auto& view = *source.view;
            view.render = view.schedule.due(source.camera->render_type,view.target->needsResize());
            view.ticket = view.schedule.ticket();
            view.target->prepare(*this);
            if (view.render) {
                view.background = source.camera->background;
                view.engine = details::cameraFrame(*source.camera,*source.transform,view.target->width(),view.target->height(),time);
            }
            _views.push_back(source.view);
        }
        std::sort(_views.begin(),_views.end(),[](const auto& a,const auto& b) { return a->id < b->id; });
        if (_primaryView) _engineVariableBuffer = _primaryView->engine;
    }
    void Renderer::clearCamera()
    {
        auto* data = recording();
        if (!data) throw std::logic_error("Camera clear requires a render pass");
        clearDepth(); clearColor(&data->background.x);
    }
    void Renderer::clearColor(const float color[4])
    {
        auto* data = recording();
        if (!data || !data->colorTarget.ptr) throw std::logic_error("Clear requires a declared color target");
        data->commands.raw()->ClearRenderTargetView(data->colorTarget,color,0,nullptr);
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
