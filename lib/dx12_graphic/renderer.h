#ifndef __CSYREN_DX12_RENDERER__
#define __CSYREN_DX12_RENDERER__

#include <Windows.h>
#include <wrl.h>
#include <dxgi1_4.h>
#include <d3d12.h>
#include "d3dcompiler.h"
#include <DirectXMath.h>
#include <DirectXTex.h>

#include <string>
#include <vector>
#include <memory>

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


namespace csyren::render
{
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

	class Renderer
	{
	public:
		Renderer() noexcept = default;
		~Renderer();

		Renderer(const Renderer&) = delete;
		Renderer& operator=(const Renderer&) = delete;

		bool earlyInit();
		void init();
		void beginFrame();
		void clear(const FLOAT color[4]);
		void endFrame();


		void beginResourceUpload();
		void endResourceUpload();

		ID3D12GraphicsCommandList* commandList() const noexcept { return _cmdLists[_frameIndex].raw(); }
		ID3D12Device* device() const noexcept { return _device.Get(); }

		UploadRingBuffer* getUploadBuffer() noexcept { return &_perEntityCB; };

		EngineVariableBuffer* getEngineVariableBuffer() noexcept { return &_engineVariableBuffer; };
		EntityVariableBuffer* getEntityVariableBuffer() noexcept { return &_entityVariableBuffer; };

		bool bindMaterial(ResourceManager& rm, MaterialHandle material,const VertexLayout& vertexLayout);

		bool bindEntity(const SemanticBufferLayout* layout);
	private:
		void waitForGpu();
		void resizeSwapChain(uint32_t width, uint32_t height);
		void enableDebugLayer();
		void createFactory();
		void createDevice();
		void createCommandQueue();
		void createSwapChain(HWND hwnd,uint32_t width, uint32_t height);

		static constexpr UINT FrameCount = 2;

		details::ComPtr<ID3D12Device>			_device;
		details::ComPtr<IDXGISwapChain3>		_swapChain;
		details::ComPtr<IDXGIFactory4>			_factory;

		details::ComPtr<ID3D12Resource> _renderTargets[FrameCount];
		DescriptorAllocation			_rtv[FrameCount];
		details::ComPtr<ID3D12Resource> _depthStencil;
		DescriptorAllocation			_dsv;

		D3D12_RESOURCE_STATES _depthStencilCurrentState = D3D12_RESOURCE_STATE_DEPTH_WRITE;


		CommandList								_cmdLists[FrameCount];
		RenderQueue								_mainQueue;

		UploadRingBuffer						_perEntityCB;
		EngineVariableBuffer					_engineVariableBuffer;
		EntityVariableBuffer					_entityVariableBuffer;
		ShaderParameterBinder					_parameterBinder;

		uint32_t								 _width;
		uint32_t								 _height;
		uint8_t									_frameIndex;
		uint8_t									_lastBackBuffer;

		bool									_enableVSync{ false };
	};
}

#endif
