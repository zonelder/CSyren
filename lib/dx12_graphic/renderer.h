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

#include "descriptor_heap_manager.h"
#include "sampler_manager.h"
#include "constant_buffer.h"
#include "upload_ring_buffer.h"
#include "engine_semantics.h"
#include "pso_factory.h"
#include "shader_parameter_binder.h"
#include "vertex_layout.h"

#include "render_queue.h"


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

		bool init(HWND hwnd, UINT width, UINT height);
		void beginFrame();
		void clear(const FLOAT color[4]);
		void endFrame();


		void beginResourceUpload();
		void endResourceUpload();

		DescriptorHeapManager* getDescriptorHeapManager() const noexcept { return _pSrvHeapManager.get(); }

		ID3D12GraphicsCommandList* commandList() const noexcept { return _commandList.Get(); }
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
		details::ComPtr<ID3D12CommandQueue>		_commandQueue;
		details::ComPtr<ID3D12DescriptorHeap>	_rtvHeap;
		details::ComPtr<ID3D12DescriptorHeap>	_dsvHeap;
		details::ComPtr<IDXGIFactory4>			_factory;
		UINT _dsvDescriptorSize = 0;
		UINT _rtvDescriptorSize{ 0 };

		D3D12_VIEWPORT _viewport{};
		D3D12_RECT		_scissor{};
		details::ComPtr<ID3D12Resource> _renderTargets[FrameCount];
		details::ComPtr<ID3D12Resource> _depthStencil;
		D3D12_RESOURCE_STATES _depthStencilCurrentState = D3D12_RESOURCE_STATE_DEPTH_WRITE;
		details::ComPtr<ID3D12CommandAllocator> _commandAllocator;
		details::ComPtr<ID3D12GraphicsCommandList> _commandList;
		
		RenderQueue _mainQueue;

		UploadRingBuffer _perEntityCB;
		EngineVariableBuffer					_engineVariableBuffer;
		EntityVariableBuffer					_entityVariableBuffer;
		ShaderParameterBinder					_parameterBinder;

		std::unique_ptr<details::PSOFactory>	_pPSOFactory;
		std::unique_ptr<DescriptorHeapManager>	_pSrvHeapManager;
		bool									_enableVSync{ false };
	};
}

#endif
