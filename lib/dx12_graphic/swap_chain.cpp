#include "pch.h"
#include "swap_chain.h"


namespace csyren::render
{
	bool SwapChain::init(IDXGIFactory4* factory,
		ID3D12Device* device,
		details::ComPtr< ID3D12CommandQueue > graphicsQueue,
		HWND hwnd,
		const DXGI_SWAP_CHAIN_DESC1& desc)
	{
		_pQueue = graphicsQueue;
		_tearingSupported = checkTearingSupport(factory);
		DXGI_SWAP_CHAIN_DESC1 swapDesc = desc;
		swapDesc.Scaling = DXGI_SCALING_NONE;

		_swapChainFlags = swapDesc.Flags;
		if (_tearingSupported)
			_swapChainFlags |= DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING;
		swapDesc.Flags = _swapChainFlags;


		details::ComPtr< IDXGISwapChain1 > sc1;
		if (DX_FAILED(factory->CreateSwapChainForHwnd(_pQueue.Get(),
			hwnd,
			&swapDesc,
			nullptr,
			nullptr,
			&sc1)))
		{
			swapDesc.Scaling = DXGI_SCALING_STRETCH;
			if (DX_FAILED(factory->CreateSwapChainForHwnd(_pQueue.Get(), hwnd, &swapDesc, nullptr, nullptr, &sc1)))
			{
				return false;
			}
		}

		if (DX_FAILED(sc1.As(&_pSwapChain)))
		{
			return false;
		}
		if (!collectResources())
			return false;

		_meta.format = desc.Format;
		_meta.width = desc.Width;
		_meta.height = desc.Height;
		_meta.flags = desc.Flags;
		return true;
	}

	bool SwapChain::checkTearingSupport(IDXGIFactory4* factory)
	{
		details::ComPtr< IDXGIFactory5 > factory5;
		if (DX_SUCCEEDED(factory->QueryInterface(IID_PPV_ARGS(&factory5))))
		{
			BOOL allowTearing = FALSE;
			if (DX_SUCCEEDED(factory5->CheckFeatureSupport(DXGI_FEATURE_PRESENT_ALLOW_TEARING,
				&allowTearing,
				sizeof(allowTearing))))
			{
				return allowTearing == TRUE;
			}
		}
		return false;
	}


	bool SwapChain::resize(uint32_t width, uint32_t height, bool windowed)
	{
		if (width == _meta.width && height == _meta.height)
			return true;

		if (width == 0 || height == 0)
			return false;

		releaseResources();

		UINT resizeFlags = _swapChainFlags;
		if (!windowed)
			resizeFlags |= DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;

		if (DX_FAILED(_pSwapChain->ResizeBuffers(_backBuffers.size(),
			width,
			height,
			_meta.format,	// DXGI_FORMAT_UNKNOWN = «оставить текущий формат»
			resizeFlags)))
		{
			return false;
		}
		_meta.width = width;
		_meta.height = height;

		if (!collectResources())
			return false;


		return true;
	}

	bool SwapChain::present(UINT vsync, UINT presentFlags)
	{
		if (vsync == 0 && _tearingSupported)
			presentFlags |= DXGI_PRESENT_ALLOW_TEARING;

		if (DX_FAILED(_pSwapChain->Present(vsync, presentFlags)))
		{
			return false;
		}
		return true;
	}


	bool SwapChain::releaseResources()
	{
		bool result = true;
		for (auto& buffer : _backBuffers)
		{
			buffer->Release();
			buffer = nullptr;
		}
		return result;
	}

	bool SwapChain::collectResources()
	{
		for (uint32_t i = 0; i < _backBuffers.size(); ++i)
		{
			_pSwapChain->GetBuffer(i, IID_PPV_ARGS(&_backBuffers[i]));
		}
		return true;
	}

}

