#ifndef __DESCRIPTOR_HEAP_HPP__
#define __DESCRIPTOR_HEAP_HPP__
#include "dx_main.h"
#include "cstdmf/assert_helpler.h"

#include <vector>

namespace csyren::render
{
	class DescriptorHeap
	{
	public:
		void init(ID3D12Device* device, D3D12_DESCRIPTOR_HEAP_TYPE type, uint32_t capacity, bool shaderVisible)
		{
			D3D12_DESCRIPTOR_HEAP_DESC desc = {};
			desc.Type = type;
			desc.NumDescriptors = capacity;
			desc.Flags = shaderVisible ? D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE : D3D12_DESCRIPTOR_HEAP_FLAG_NONE;

			device->CreateDescriptorHeap(&desc, IID_PPV_ARGS(&heap_));

			increment_ = device->GetDescriptorHandleIncrementSize(type);
			capacity_ = capacity;
			type_ = type;
			used_ = 0;
			freeList_.clear();
		}

		size_t capacity() const noexcept
		{
			return capacity_;
		}

		/// Number of descriptors currently in use (allocated minus freed).
		size_t activeCount() const noexcept
		{
			return used_ - freeList_.size();
		}

		/// Number of descriptors available (free list + unallocated bump space).
		size_t availableCount() const noexcept
		{
			return freeList_.size() + (capacity_ - used_);
		}

		D3D12_DESCRIPTOR_HEAP_TYPE type() const noexcept
		{
			return type_;
		}

		uint32_t allocate()
		{
			if (!freeList_.empty())
			{
				uint32_t idx = freeList_.back();
				freeList_.pop_back();
				return idx;
			}
			// Иначе bump-аллокация с конца.
			if (used_ >= capacity_)
			{
				log::error("DescriptorHeap::allocate: heap type %d is full "
					"(%u/%u). Increase capacity!\n",
					(int)type_, used_, capacity_);
				CS_ASSERT(false && "DescriptorHeap overflow");
			}
			return used_++;
		}

		void free(uint32_t index)
		{
			CS_ASSERT(index < used_);
			freeList_.push_back(index);
		}

		D3D12_CPU_DESCRIPTOR_HANDLE cpu(uint32_t index) const
		{
			auto h = heap_->GetCPUDescriptorHandleForHeapStart();
			h.ptr += index * increment_;
			return h;
		}

		D3D12_GPU_DESCRIPTOR_HANDLE gpu(uint32_t index) const
		{
			auto h = heap_->GetGPUDescriptorHandleForHeapStart();
			h.ptr += index * increment_;
			return h;
		}

		ID3D12DescriptorHeap* heap() const
		{
			return heap_.Get();
		}

	private:
		details::ComPtr< ID3D12DescriptorHeap > heap_;
		D3D12_DESCRIPTOR_HEAP_TYPE type_{};
		uint32_t increment_ = 0;
		uint32_t capacity_ = 0;
		uint32_t used_ = 0;
		std::vector< uint32_t > freeList_;
	};
}

#endif






