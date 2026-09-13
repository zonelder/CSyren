#include "pch.h"
#include "descriptors.h"

namespace csyren::render
{
	void DescriptorManager::init()
	{
		auto device = core::Services::get<Renderer>()->device();
		CS_DEBUG_ASSERT(device != nullptr);
		_device = device;
		rtvHeap_.init(device, D3D12_DESCRIPTOR_HEAP_TYPE_RTV, 256, false);

		dsvHeap_.init(device, D3D12_DESCRIPTOR_HEAP_TYPE_DSV, 64, false);

		// One SRV (or UAV/CBV slot) per allocation; large worlds / ME stress tests can hold
		// many live textures plus materials and buffers. 2048 overflowed bindless-style use.
		srvHeap_.init(device, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, kShaderVisibleSrvUavHeapCapacity, true);
	}
}