#pragma once

#include "descriptor_heap_manager.h"

namespace csyren::render
{
	struct DescriptorHandles
	{
		D3D12_CPU_DESCRIPTOR_HANDLE cpuHandle;
		D3D12_GPU_DESCRIPTOR_HANDLE gpuHandle;

		uint32_t slot;

		bool isValid() const { return cpuHandle.ptr != 0; }
	};

	class Descriptors
	{
	public:
		

	private:
		DescriptorHeap srv_;
		DescriptorHeap rtv_;
		DescriptorHeap dsv_;
	};
}