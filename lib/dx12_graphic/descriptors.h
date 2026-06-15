#ifndef __DESCRIPTOR_MANAGER_HPP__
#define __DESCRIPTOR_MANAGER_HPP__
#include "dx_main.h"
#include "core/services.h"
#include "descriptor_allocation.h"
#include "descriptor_heap.h"

#include "cstdmf/assert_helpler.h"

namespace csyren::render
{

	class DescriptorManager
	{
	public:
		

		void init();

		// --- RTV ------------------------------------------------

		DescriptorAllocation createRTV(ID3D12Resource* resource, const D3D12_RENDER_TARGET_VIEW_DESC* desc = nullptr)
		{
			uint32_t idx = rtvHeap_.allocate();
			auto cpu = rtvHeap_.cpu(idx);
			_device->CreateRenderTargetView(resource, desc, cpu);
			return { cpu, {}, idx };
		}

		void recreateRTV(DescriptorAllocation& allocation,
			ID3D12Resource* resource,
			const D3D12_RENDER_TARGET_VIEW_DESC* desc = nullptr)
		{
			CS_ASSERT(allocation.handle_ < rtvHeap_.capacity());
			auto cpu = rtvHeap_.cpu(allocation.handle_);
			_device->CreateRenderTargetView(resource, desc, cpu);
			allocation.cpu = rtvHeap_.cpu(allocation.handle_);
		}

		void freeRTV(DescriptorAllocation& allocation)
		{
			if (allocation.valid())
			{
				rtvHeap_.free(allocation.handle_);
				allocation.invalidate();
			}
		}

		// --- DSV ------------------------------------------------

		DescriptorAllocation createDSV(ID3D12Resource* resource, const D3D12_DEPTH_STENCIL_VIEW_DESC* desc = nullptr)
		{
			uint32_t idx = dsvHeap_.allocate();
			auto cpu = dsvHeap_.cpu(idx);
			_device->CreateDepthStencilView(resource, desc, cpu);
			return { cpu, {}, idx };
		};

		void recreateDSV(DescriptorAllocation& allocation,
			ID3D12Resource* resource,
			const D3D12_DEPTH_STENCIL_VIEW_DESC* desc = nullptr)
		{
			CS_ASSERT(allocation.handle_ < dsvHeap_.capacity());
			auto cpu = dsvHeap_.cpu(allocation.handle_);
			_device->CreateDepthStencilView(resource, desc, cpu);
			allocation.cpu = dsvHeap_.cpu(allocation.handle_);
		}

		void freeDSV(DescriptorAllocation& allocation)
		{
			if (allocation.valid())
			{
				dsvHeap_.free(allocation.handle_);
				allocation.invalidate();
			}
		}

		// --- SRV ------------------------------------------------

		DescriptorAllocation createSRV(ID3D12Resource* resource, const D3D12_SHADER_RESOURCE_VIEW_DESC* desc)
		{
			uint32_t idx = srvHeap_.allocate();
			auto cpu = srvHeap_.cpu(idx);
			_device->CreateShaderResourceView(resource, desc, cpu);
			return { cpu, srvHeap_.gpu(idx), idx };
		};

		void recreateSRV(DescriptorAllocation& allocation,
			ID3D12Resource* resource,
			const D3D12_SHADER_RESOURCE_VIEW_DESC* desc)
		{
			CS_ASSERT(allocation.handle_ < srvHeap_.capacity());
			auto cpu = srvHeap_.cpu(allocation.handle_);
			_device->CreateShaderResourceView(resource, desc, cpu);
			allocation.cpu = srvHeap_.cpu(allocation.handle_);
			allocation.gpu = srvHeap_.gpu(allocation.handle_);
		}

		void freeSRV(DescriptorAllocation& allocation)
		{
			if (allocation.valid())
			{
				srvHeap_.free(allocation.handle_);
				allocation.invalidate();
			}
		}

		// --- UAV ------------------------------------------------

		DescriptorAllocation createUAV(ID3D12Resource* resource,
			const D3D12_UNORDERED_ACCESS_VIEW_DESC* desc,
			ID3D12Resource* counterResource = nullptr)
		{
			// UAV ???? ? ??? ?? CBV_SRV_UAV ????, ??? ? SRV.
			uint32_t idx = srvHeap_.allocate();
			auto cpu = srvHeap_.cpu(idx);
			_device->CreateUnorderedAccessView(resource, counterResource, desc, cpu);
			return { cpu, srvHeap_.gpu(idx), idx };
		}

		void recreateUAV(DescriptorAllocation& allocation,
			ID3D12Resource* resource,
			const D3D12_UNORDERED_ACCESS_VIEW_DESC* desc,
			ID3D12Resource* counterResource = nullptr)
		{
			CS_ASSERT(allocation.handle_ < srvHeap_.capacity());
			auto cpu = srvHeap_.cpu(allocation.handle_);
			_device->CreateUnorderedAccessView(resource, counterResource, desc, cpu);
			allocation.cpu = srvHeap_.cpu(allocation.handle_);
			allocation.gpu = srvHeap_.gpu(allocation.handle_);
		}

		void freeUAV(DescriptorAllocation& allocation)
		{
			// UAV ????? ????? ? srvHeap_ ? ??? ?? free list.
			if (allocation.valid())
			{
				srvHeap_.free(allocation.handle_);
				allocation.invalidate();
			}
		}

		// --- Heap access ----------------------------------------

		ID3D12DescriptorHeap* shaderHeap() const
		{
			return srvHeap_.heap();
		}

		D3D12_GPU_DESCRIPTOR_HANDLE gpuSRV(uint32_t idx) const
		{
			return srvHeap_.gpu(idx);
		}
		D3D12_GPU_DESCRIPTOR_HANDLE gpuDSV(uint32_t idx) const
		{
			return dsvHeap_.gpu(idx);
		}

		// --- Diagnostics ----------------------------------------

		struct HeapStats
		{
			size_t capacity;
			size_t active;
			size_t available;
		};

		HeapStats rtvStats() const noexcept
		{
			return { rtvHeap_.capacity(), rtvHeap_.activeCount(), rtvHeap_.availableCount() };
		}

		HeapStats dsvStats() const noexcept
		{
			return { dsvHeap_.capacity(), dsvHeap_.activeCount(), dsvHeap_.availableCount() };
		}

		HeapStats srvUavStats() const noexcept
		{
			return { srvHeap_.capacity(), srvHeap_.activeCount(), srvHeap_.availableCount() };
		}


	private:
		static constexpr uint32_t kShaderVisibleSrvUavHeapCapacity = 65536;

		ID3D12Device*  _device;
		DescriptorHeap rtvHeap_;
		DescriptorHeap dsvHeap_;
		DescriptorHeap srvHeap_;
	};
}  // namespace Moo

#endif