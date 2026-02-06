#pragma once
#include <d3d12.h>
#include <wrl.h>



namespace csyren::render::details
{
	template<class T>
	using ComPtr = Microsoft::WRL::ComPtr<T>;

	using Resource = ID3D12Resource;

}