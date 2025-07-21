#include "pch.h"
#include "descriptor_heap_manager.h"


namespace csyren::render
{
    bool DescriptorHeapManager::init(ID3D12Device* device, D3D12_DESCRIPTOR_HEAP_TYPE type, UINT descriptorsPerHeap, bool isShaderVisible)
    {
        _device = device;
        _heapType = type;
        _descriptorsPerHeap = descriptorsPerHeap;
        _isShaderVisible = isShaderVisible;
        _descriptorSize = _device->GetDescriptorHandleIncrementSize(_heapType);
        _currentHeapIndex = 0;
        _currentOffsetInHeap = 0;

        return createNewHeap();
    }


    DescriptorHandles DescriptorHeapManager::allocate()
    {
        if (!_freeList.empty())
        {
            DescriptorHandles handles = _freeList.back();
            _freeList.pop_back();
            return handles;
        }

        if (_currentOffsetInHeap >= _descriptorsPerHeap)
        {
            _currentHeapIndex++;
            _currentOffsetInHeap = 0;
            if (_currentHeapIndex >= _heapPool.size())
            {
                if (!createNewHeap())
                {
                    return { {0}, {0} };
                }
            }
        }

        ID3D12DescriptorHeap* currentHeap = _heapPool[_currentHeapIndex].Get();
        D3D12_CPU_DESCRIPTOR_HANDLE cpuHandle = currentHeap->GetCPUDescriptorHandleForHeapStart();
        cpuHandle.ptr += static_cast<SIZE_T>(_currentOffsetInHeap) * _descriptorSize;

        D3D12_GPU_DESCRIPTOR_HANDLE gpuHandle = { 0 };
        if (_isShaderVisible)
        {
            gpuHandle = currentHeap->GetGPUDescriptorHandleForHeapStart();
            gpuHandle.ptr += static_cast<SIZE_T>(_currentOffsetInHeap) * _descriptorSize;
        }

        _currentOffsetInHeap++;

        return { cpuHandle, gpuHandle };
    }

    void DescriptorHeapManager::free(DescriptorHandles handles)
    {
        if (handles.isValid())
        {
            _freeList.push_back(handles);
        }
    }

    std::vector<ID3D12DescriptorHeap*> DescriptorHeapManager::getHeaps() const
    {
        std::vector<ID3D12DescriptorHeap*> heaps;
        heaps.reserve(_heapPool.size());
        for (const auto& heap : _heapPool)
        {
            heaps.push_back(heap.Get());
        }
        return heaps;
    }

    bool DescriptorHeapManager::createNewHeap()
    {
        D3D12_DESCRIPTOR_HEAP_DESC heapDesc = {};
        heapDesc.NumDescriptors = _descriptorsPerHeap;
        heapDesc.Type = _heapType;
        heapDesc.Flags = _isShaderVisible ? D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE : D3D12_DESCRIPTOR_HEAP_FLAG_NONE;

        Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> newHeap;
        HRESULT hr;
        if (FAILED(hr = _device->CreateDescriptorHeap(&heapDesc, IID_PPV_ARGS(&newHeap))))
        {
            log::log_hr("DescriptorHeapManager:CreateDescriptorHeap", hr);
            return false;
        }

        _heapPool.push_back(newHeap);
        return true;
    }
}
