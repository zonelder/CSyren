#ifndef __CSYREN_DESCRIPTOR_HEAP_MANAGER__
#define __CSYREN_DESCRIPTOR_HEAP_MANAGER__

#include <d3d12.h>
#include <wrl.h>
#include <vector>
#include <stdexcept>

namespace csyren::render
{
    struct DescriptorHandles
    {
        D3D12_CPU_DESCRIPTOR_HANDLE cpuHandle;
        D3D12_GPU_DESCRIPTOR_HANDLE gpuHandle;

        bool isValid() const { return cpuHandle.ptr != 0; }
    };

    class DescriptorHeapManager
    {
    public:
        DescriptorHeapManager() = default;

        bool init(
            ID3D12Device* device,
            D3D12_DESCRIPTOR_HEAP_TYPE type,
            UINT descriptorsPerHeap = 256,
            bool isShaderVisible = true);

        DescriptorHandles allocate();

        void free(DescriptorHandles handles);

        ID3D12DescriptorHeap* getHeap() const;

        DescriptorHandles getHandlesFromIndex(UINT index) const;

    private:

        Microsoft::WRL::ComPtr<ID3D12Device> _device;
        D3D12_DESCRIPTOR_HEAP_TYPE _heapType;
        UINT _numDescriptorsInHeap{ 0 };
        bool _isShaderVisible{ false };
        UINT _descriptorSize{ 0 };

        Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> _heap;

        UINT _nextFreeIndex{ 0 };

        // Список свободных дескрипторов для переиспользования
        std::vector<UINT> _freeList;
    };
}


#endif
