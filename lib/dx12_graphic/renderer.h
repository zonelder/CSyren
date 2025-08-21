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

#include "descriptor_heap_manager.h"
#include "sampler_manager.h"
#include "constant_buffer.h"

namespace csyren::render
{
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

		HRESULT uploadTextureData(ID3D12Resource* destResource, const DirectX::ScratchImage& scratchImage);
		DescriptorHeapManager* getDescriptorHeapManager() const noexcept { return _pSrvHeapManager.get(); }

		ID3D12GraphicsCommandList* commandList() const noexcept { return _commandList.Get(); }
		ID3D12Device* device() const noexcept { return _device.Get(); }

		ConstantBuffer* getPerFrameCB() noexcept { return &_perFrameCB; };

		ConstantBuffer* getPerEntityCB() noexcept { return &_perEntityCB; };

		ConstantBuffer* getPerMaterialCB() noexcept { return &_perMaterialCB; };


		PerEntityBuffer* getPerEntityBuffer() noexcept { return &_perEntityBuffer;};

		PerFrameBuffer* getPerFrameBuffer() noexcept { return &_perFrameBuffer; };

		void setTexture(size_t slot, Texture* texture);
		void setSampler(size_t slot, SamplerType type);
	private:
		void waitForGpu();

		static constexpr UINT FrameCount = 2;

		Microsoft::WRL::ComPtr<ID3D12Device> _device;
		Microsoft::WRL::ComPtr<IDXGISwapChain3> _swapChain;
		Microsoft::WRL::ComPtr<ID3D12CommandQueue> _commandQueue;
		Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> _rtvHeap;
		UINT _rtvDescriptorSize{ 0 };
		D3D12_VIEWPORT _viewport{};
		D3D12_RECT		_scissor{};
		Microsoft::WRL::ComPtr<ID3D12Resource> _renderTargets[FrameCount];
		Microsoft::WRL::ComPtr<ID3D12CommandAllocator> _commandAllocator;
		Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList> _commandList;
		Microsoft::WRL::ComPtr<ID3D12Fence> _fence;
		UINT64 _fenceValue{ 0 };
		HANDLE _fenceEvent{ nullptr };
		UINT _frameIndex{ 0 };

		ConstantBuffer _perFrameCB;
		ConstantBuffer _perEntityCB;
		ConstantBuffer _perMaterialCB;

		PerFrameBuffer _perFrameBuffer;
		PerEntityBuffer _perEntityBuffer;

		std::unique_ptr<DescriptorHeapManager> _pSrvHeapManager;
		std::unique_ptr<DescriptorHeapManager> _pSamplerHeapManager;
		SamplerManager  _samplerManager;
	};
}

#endif
