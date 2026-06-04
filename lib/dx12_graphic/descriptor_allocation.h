#pragma once

#include "dx_main.h"

#include <cstdint>

namespace csyren::render
{
	struct DescriptorAllocation
	{
		friend class DescriptorManager;

		D3D12_CPU_DESCRIPTOR_HANDLE cpu;
		D3D12_GPU_DESCRIPTOR_HANDLE gpu;
		DescriptorAllocation() noexcept : handle_(UINT32_MAX) {}


		DescriptorAllocation(DescriptorAllocation&& other) noexcept :
			cpu(other.cpu),
			gpu(other.gpu),
			handle_(other.handle_)
		{
			other.invalidate();
		}

		DescriptorAllocation& operator=(DescriptorAllocation&& other) noexcept
		{
			if (this != &other)
			{
				cpu = other.cpu;
				gpu = other.gpu;
				handle_ = other.handle_;

				other.invalidate();
			}

			return *this;
		}

		bool valid() const noexcept
		{
			return handle_ != UINT32_MAX;
		}
		explicit operator bool() const noexcept
		{
			return valid();
		}

		uint32_t heapIndex() const noexcept
		{
			return handle_;
		}

	private:
		DescriptorAllocation(D3D12_CPU_DESCRIPTOR_HANDLE c, D3D12_GPU_DESCRIPTOR_HANDLE g, uint32_t h) noexcept :
			cpu(c),
			gpu(g),
			handle_(h)
		{
		}

		void invalidate() noexcept
		{
			handle_ = UINT32_MAX;
			cpu = {};
			gpu = {};
		}

		uint32_t handle_;
	};
}