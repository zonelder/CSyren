#include "pch.h"
#include "descriptor_heap_manager.h"


namespace csyren::render
{
    bool DescriptorHeapManager::init(ID3D12Device* device, D3D12_DESCRIPTOR_HEAP_TYPE type, UINT descriptorsPerHeap, bool isShaderVisible)
    {
        _device = device;
        _heapType = type;
        _numDescriptorsInHeap = descriptorsPerHeap;
        _isShaderVisible = isShaderVisible;
        _descriptorSize = _device->GetDescriptorHandleIncrementSize(_heapType);
        _nextFreeIndex = 0;

        D3D12_DESCRIPTOR_HEAP_DESC heapDesc = {};
        heapDesc.NumDescriptors = _numDescriptorsInHeap;
        heapDesc.Type = _heapType;
        heapDesc.Flags = _isShaderVisible ? D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE : D3D12_DESCRIPTOR_HEAP_FLAG_NONE;

        HRESULT hr;
        if (DX_FAILED(hr = _device->CreateDescriptorHeap(&heapDesc, IID_PPV_ARGS(&_heap))))
        {
            return false;
        }

        return true;
    }


    DescriptorHandles DescriptorHeapManager::allocate()
    {
        UINT index;
        if (!_freeList.empty())
        {
            index = _freeList.back();
            _freeList.pop_back();
        }
        else
        {
            if (_nextFreeIndex >= _numDescriptorsInHeap)
            {
                log::error("DescriptorHeapManager::allocate() : attempt to allocate descriptor but its full: heap type = {},size = {}", static_cast<int>(_heapType), _numDescriptorsInHeap);
                return { {0},{0} };
            }
            index = _nextFreeIndex++;
        }
        return getHandlesFromIndex(index);
    }

    void DescriptorHeapManager::free(DescriptorHandles handles)
    {
        if (!handles.isValid())
        {
            log::warning("DescriptorHeapManager::free() : attempt to free invalid handlers");
            return;
        }

        D3D12_CPU_DESCRIPTOR_HANDLE heapStart = _heap->GetCPUDescriptorHandleForHeapStart();
        UINT index = static_cast<UINT>((handles.cpuHandle.ptr - heapStart.ptr) / _descriptorSize);

        _freeList.push_back(index);
    }

    DescriptorHandles DescriptorHeapManager::getHandlesFromIndex(UINT index) const
    {
        if (index >= _numDescriptorsInHeap)
        {
            log::error("DescriptorHeapManager::getHandlesFromIndex() : index out of range.index = {},maxIndex = {}", index, _numDescriptorsInHeap);
            return { {0}, {0} }; // Индекс за пределами кучи
        }

        D3D12_CPU_DESCRIPTOR_HANDLE cpuHandle = _heap->GetCPUDescriptorHandleForHeapStart();
        cpuHandle.ptr += static_cast<SIZE_T>(index) * _descriptorSize;

        D3D12_GPU_DESCRIPTOR_HANDLE gpuHandle = { 0 };
        if (_isShaderVisible)
        {
            gpuHandle = _heap->GetGPUDescriptorHandleForHeapStart();
            gpuHandle.ptr += static_cast<SIZE_T>(index) * _descriptorSize;
        }

        return { cpuHandle, gpuHandle };
    }

    ID3D12DescriptorHeap* DescriptorHeapManager::getHeap() const
    {
        return _heap.Get();
    }
}
