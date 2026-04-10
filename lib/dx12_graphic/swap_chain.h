#ifndef __SWAP_CHAIN_HPP__
#define __SWAP_CHAIN_HPP__

#include "dx_main.h"

#include <dxgi1_5.h>
#include <cstdint>
#include <vector>


namespace csyren::render
{
	class SwapChain
	{
	public:

		//@brief information about current state of SwapChain. maybe we should erase this class in release build.
		struct Meta
		{
			uint32_t width, height;
			DXGI_FORMAT format;
			uint32_t flags;
			uint32_t count;

		};
		bool init(IDXGIFactory4* factory,
			ID3D12Device* device,
			details::ComPtr<ID3D12CommandQueue> graphicsQueue,
			HWND hwnd,
			const DXGI_SWAP_CHAIN_DESC1& desc);
		bool resize(uint32_t width, uint32_t height, bool windowed);

		bool checkTearingSupport(IDXGIFactory4* factory);

		uint8_t backBufferIndex() const noexcept
		{
			return _pSwapChain->GetCurrentBackBufferIndex();
		}


		bool present(UINT syncInterval, UINT presentFlags = 0);

		ID3D12Resource* currentBackBuffer() const noexcept
		{
			return _backBuffers[backBufferIndex()];
		}

		const Meta& meta() const noexcept { return _meta; }
	private:
		bool collectResources();
		bool releaseResources();
		details::ComPtr< ID3D12CommandQueue > _pQueue;
		details::ComPtr< IDXGISwapChain3 > _pSwapChain;

		std::vector<ID3D12Resource*> _backBuffers;
		uint8_t _frameIndex{ 0u };
		Meta _meta;
		UINT _swapChainFlags{ 0u };
		bool _tearingSupported{ false };
	};
}


#endif