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

        std::vector<ID3D12DescriptorHeap*> getHeaps() const;

    private:
        // Вспомогательная функция для создания новой кучи в нашем пуле
        bool createNewHeap();

        Microsoft::WRL::ComPtr<ID3D12Device> _device;
        D3D12_DESCRIPTOR_HEAP_TYPE _heapType;
        UINT _descriptorsPerHeap{ 0 };
        bool _isShaderVisible{ false };
        UINT _descriptorSize{ 0 };

        // Пул куч дескрипторов
        std::vector<Microsoft::WRL::ComPtr<ID3D12DescriptorHeap>> _heapPool;

        // Индекс текущей кучи, из которой происходит выделение
        UINT _currentHeapIndex{ 0 };
        // Смещение внутри текущей кучи
        UINT _currentOffsetInHeap{ 0 };

        // Список свободных дескрипторов для переиспользования
        std::vector<DescriptorHandles> _freeList;
    };
}


#endif
