#ifndef __CSYREN_DX12_RENDERER__
#define __CSYREN_DX12_RENDERER__

#include "cstdmf/window_clean.h"

#include <wrl.h>
#include <dxgi1_4.h>
#include <d3d12.h>
#include "d3dcompiler.h"
#include <DirectXMath.h>
#include <DirectXTex.h>

#include <string>
#include <vector>
#include <memory>
#include <thread>

#include "dx_main.h"

#include "upload_ring_buffer.h"
#include "engine_semantics.h"
#include "pso_factory.h"
#include "shader_parameter_binder.h"
#include "vertex_layout.h"

#include "render_queue.h"
#include "descriptor_allocation.h"

#include "core/services.h"
#include "core/camera.h"
#include "camera_render_registry.h"
#include "core/transform_hierarchy.h"


namespace csyren::render
{
    namespace details { class FrameGraphBackend; }
	//class ResourceManager;

	struct  alignas(16) PerFrameBuffer
	{
		DirectX::XMMATRIX projection;
		DirectX::XMMATRIX view;
		DirectX::XMMATRIX viewProjection;
		DirectX::XMMATRIX invView;
	};

	struct PerEntityBuffer
	{
		DirectX::XMMATRIX world;
	};
	class Texture;
    class CameraRenderTarget;
    class ShaderBase;
    class ComputeShader;

	class Renderer
	{
	public:
		static constexpr UINT FramesInFlight = 3;
		Renderer() noexcept = default;
		~Renderer();

		Renderer(const Renderer&) = delete;
		Renderer& operator=(const Renderer&) = delete;

		bool earlyInit();
        // A 1x1 white texture bound in place of a material texture that is not loaded
        // (still loading, failed or unloaded), so its descriptor table is always set.
        // Call once at startup, after the DescriptorManager is initialized.
        void createFallbackTexture();



		void beginResourceUpload();
		void endResourceUpload();
        // True while GPU uploads can be recorded: inside the startup upload window
        // or while a frame task is recording. Otherwise resources must use
        // CPU-visible (upload heap) storage.
        bool canUploadNow() const noexcept { return recording() != nullptr || _uploadOpen; }

		ID3D12GraphicsCommandList* commandList() const;
		ID3D12Device* device() const noexcept { return _device.Get(); }

		UploadRingBuffer* getUploadBuffer();

		EngineVariableBuffer* getEngineVariableBuffer();
		EntityVariableBuffer* getEntityVariableBuffer();

		bool bindMaterial(const ResourceManager& rm, MaterialHandle material,const VertexLayout& vertexLayout);

		bool bindEntity(const SemanticBufferLayout* layout);

        // ---- Pass-level drawing API (valid while a GPU task records) ----
        // Pipeline for a shader that is not wrapped in a material (fullscreen
        // passes). Render target formats always come from the pass; the frame
        // semantic cbuffer is bound.
        bool bindShader(const ResourceManager& rm, ShaderHandle shader, const MaterialStateDesc& states, const VertexLayout& vertexLayout);
        bool bindComputeShader(const ResourceManager& rm, ComputeShaderHandle shader);
        // Resource binding by the shader's variable name (root parameter lookup).
        bool setShaderTexture(const ShaderBase& shader, const std::string& name, D3D12_GPU_DESCRIPTOR_HANDLE srv);
        bool setShaderBuffer(const ShaderBase& shader, const std::string& name, D3D12_GPU_VIRTUAL_ADDRESS address);   // StructuredBuffer / ByteAddressBuffer (root SRV)
        bool setShaderRWBuffer(const ShaderBase& shader, const std::string& name, D3D12_GPU_VIRTUAL_ADDRESS address); // RWStructuredBuffer (root UAV)
        bool setShaderConstants(const ShaderBase& shader, const std::string& name, const void* data, size_t size);    // cbuffer (root CBV, transient upload)
        // Transient upload memory that lives until the pass's command list retires.
        bool allocateUpload(size_t size, void** cpu, D3D12_GPU_VIRTUAL_ADDRESS* gpu);
        void drawFullscreen();
        void dispatch(UINT x, UINT y = 1, UINT z = 1);
        // Attachments of the camera view being recorded (nullptr for frame passes).
        const CameraRenderTarget* currentTarget() const;
        UINT currentWidth() const;
        UINT currentHeight() const;
        // Material states with the render target formats of the current pass.
        MaterialStateDesc passStates(MaterialStateDesc states) const;

        using CameraSource = std::function<std::pair<const core::Camera*,const core::Transform*>()>;
        std::shared_ptr<details::CameraRenderView> addCameraView(CameraSource source);
        void removeCameraView(const std::shared_ptr<details::CameraRenderView>& view);
        void presentCameraOutput(bool enabled) { _presentCameraOutput = enabled; }
        bool presentsCameraOutput() const { return _presentCameraOutput; }
        std::shared_ptr<CameraRenderTarget> primaryCameraTarget() const { return _primaryView ? _primaryView->target : nullptr; }
        void attachCameraScene(core::Scene& scene) { _cameraViews.attach(scene); }
        void detachCameraScene() { _views.clear(); _primaryView.reset(); _cameraViews.detach(); }
        // Cameras with a Transform; the world matrices are computed for every
        // Transform (the query's select<Transform>()).
        using CameraQuery = core::Select<core::Camera, core::Transform>;
        void prepareCameraViews(const CameraQuery& cameras,float time,UINT width,UINT height);
        const core::WorldTransforms& worldTransforms() const { return _worldTransforms; }
        const auto& cameraViews() const { return _views; }
        const auto& outputView() const { return _primaryView; }
        bool requestCameraRender(core::Entity::ID id) { return _cameraViews.request(id); }
        void requestPrimaryCameraRender() { if (_primaryView) _primaryView->schedule.request(); }
        // Whether the primary camera's output is shown while it is not presented
        // (the editor's Game view). Not shown: the primary camera is not rendered.
        void showPrimaryCamera(bool shown) { _primaryShown = shown; }
        void completeCameraViews() { for (auto& view : _views) if (view->render) view->schedule.submitted(view->ticket); }
        // Scene lighting inputs for the next prepared camera views (written before FrameAcquireSystem).
        RenderEnvironment& environment() noexcept { return _environment; }
        const RenderEnvironment& environment() const noexcept { return _environment; }
        // Owner-thread API: resources retired while recording get this frame's final fence.
        void deferRelease(std::shared_ptr<void> resource);
        void collectDeferredReleases();
        void flushDeferredReleases(); // Shutdown between frames, after recording has stopped.
        void clearCamera();
        void clearColor(const float color[4]);
        void clearDepth();

		RenderQueue& queue() noexcept
		{
			return _mainQueue;
		}
	private:
        friend class details::FrameGraphBackend;
        void completeFrame(uint64_t fence);
        struct DeferredRelease { std::shared_ptr<void> resource; uint64_t fence = 0; };
        std::vector<DeferredRelease> _deferredReleases;
        struct RecordingData {
            CommandList commands;
            UploadRingBuffer upload;
            EngineVariableBuffer engine;
            EntityVariableBuffer entity;
            ShaderParameterBinder binder;
            D3D12_CPU_DESCRIPTOR_HANDLE colorTarget{},depthTarget{};
            D3D12_CPU_DESCRIPTOR_HANDLE colorTargets[D3D12_SIMULTANEOUS_RENDER_TARGET_COUNT]{};
            DXGI_FORMAT colorFormats[D3D12_SIMULTANEOUS_RENDER_TARGET_COUNT]{};
            DXGI_FORMAT depthFormat = DXGI_FORMAT_UNKNOWN;
            UINT colorCount = 0;
            UINT width = 0, height = 0;
            CameraRenderTarget* target = nullptr;
            math::Vector4 background{};
        };
        struct RecordingBinding { Renderer* renderer; RecordingData* data; };
        inline static thread_local RecordingBinding* _recording = nullptr;
        RecordingData* recording() const;
        RecordingData& requireRecording(const char* what) const;
		void waitForGpu();
		void enableDebugLayer();
		void createDevice();
		void createCommandQueue();

        details::ComPtr<ID3D12Device> _device;
        CommandList _uploadCommands;
        std::thread::id _ownerThread;
		RenderQueue								_mainQueue;

		UploadRingBuffer						_perEntityCB;
		EngineVariableBuffer					_engineVariableBuffer;
		EntityVariableBuffer					_entityVariableBuffer;
		ShaderParameterBinder					_parameterBinder;
        details::CameraRenderRegistry _cameraViews;
        core::WorldTransforms _worldTransforms;
        RenderEnvironment _environment;
        struct ExternalCamera { CameraSource source; std::shared_ptr<details::CameraRenderView> view; };
        std::vector<ExternalCamera> _externalCameras;
        bool _presentCameraOutput = true;
        bool _primaryShown = true;
        bool _uploadOpen = false;
        details::ComPtr<ID3D12Resource> _fallbackTexture;
        DescriptorAllocation _fallbackSrv;
        std::shared_ptr<details::CameraRenderView> _primaryView;
        std::vector<std::shared_ptr<details::CameraRenderView>> _views;

	};
}

#endif
