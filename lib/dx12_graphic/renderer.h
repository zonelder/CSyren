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

namespace csyren::render
{
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


		std::unique_ptr<DescriptorHeapManager> _pSrvHeapManager;
	};
}

#endif
