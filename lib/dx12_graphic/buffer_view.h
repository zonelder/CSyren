#ifndef __BUFFER_VIEW_HPP__
#define __BUFFER_VIEW_HPP__
#include "dx_main.h"
#include "cstdmf/inline_helper.h"

namespace csyren::render
{
	struct VertexBufferView
	{
		friend class CommandList;

		constexpr VertexBufferView() noexcept = default;
		constexpr VertexBufferView(D3D12_GPU_VIRTUAL_ADDRESS addr, UINT stride, UINT size) noexcept : dx_{ addr, size, stride } {}

		CS_FORCE_INLINE constexpr operator const D3D12_VERTEX_BUFFER_VIEW& () const noexcept { return dx_; }

	private:
		D3D12_VERTEX_BUFFER_VIEW dx_{};
	};

	struct IndexBufferView
	{
		friend class CommandList;

		constexpr IndexBufferView() noexcept = default;
		constexpr IndexBufferView(D3D12_GPU_VIRTUAL_ADDRESS addr, UINT size, DXGI_FORMAT format) noexcept : dx_{ addr, size, format } {}

		CS_FORCE_INLINE constexpr operator const D3D12_INDEX_BUFFER_VIEW& () const noexcept { return dx_; }

	private:
		D3D12_INDEX_BUFFER_VIEW dx_{};
	};
}

#endif