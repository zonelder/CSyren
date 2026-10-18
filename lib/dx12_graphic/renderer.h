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

#include "sampler_manager.h"
#include "constant_buffer.h"
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

	class Renderer
	{
	public:
		static constexpr UINT FramesInFlight = 3;
		Renderer() noexcept = default;
		~Renderer();

		Renderer(const Renderer&) = delete;
		Renderer& operator=(const Renderer&) = delete;

		bool earlyInit();



		void beginResourceUpload();
		void endResourceUpload();

		ID3D12GraphicsCommandList* commandList() const;
		ID3D12Device* device() const noexcept { return _device.Get(); }

		UploadRingBuffer* getUploadBuffer();

		EngineVariableBuffer* getEngineVariableBuffer();
		EntityVariableBuffer* getEntityVariableBuffer();

		bool bindMaterial(const ResourceManager& rm, MaterialHandle material,const VertexLayout& vertexLayout);

		bool bindEntity(const SemanticBufferLayout* layout);
        using CameraSource = std::function<std::pair<const core::Camera*,const core::Transform*>()>;
        std::shared_ptr<details::CameraRenderView> addCameraView(CameraSource source);
        void removeCameraView(const std::shared_ptr<details::CameraRenderView>& view);
        void presentCameraOutput(bool enabled) { _presentCameraOutput = enabled; }
        bool presentsCameraOutput() const { return _presentCameraOutput; }
        std::shared_ptr<CameraRenderTarget> primaryCameraTarget() const { return _primaryView ? _primaryView->target : nullptr; }
        void attachCameraScene(core::Scene& scene) { _cameraViews.attach(scene); }
        void detachCameraScene() { _views.clear(); _primaryView.reset(); _cameraViews.detach(); }
        void prepareCameraViews(core::Scene& scene,UINT width,UINT height);
        const core::WorldTransforms& worldTransforms() const { return _worldTransforms; }
        const auto& cameraViews() const { return _views; }
        const auto& outputView() const { return _primaryView; }
        bool requestCameraRender(core::Entity::ID id) { return _cameraViews.request(id); }
        void requestPrimaryCameraRender() { if (_primaryView) _primaryView->schedule.request(); }
        void completeCameraViews() { for (auto& view : _views) if (view->render) view->schedule.submitted(view->ticket); }
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
            math::Vector4 background{};
        };
        struct RecordingBinding { Renderer* renderer; RecordingData* data; };
        inline static thread_local RecordingBinding* _recording = nullptr;
        RecordingData* recording() const;
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
        struct ExternalCamera { CameraSource source; std::shared_ptr<details::CameraRenderView> view; };
        std::vector<ExternalCamera> _externalCameras;
        bool _presentCameraOutput = true;
        std::shared_ptr<details::CameraRenderView> _primaryView;
        std::vector<std::shared_ptr<details::CameraRenderView>> _views;

	};
}

#endif
