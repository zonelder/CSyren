#pragma once

#include "renderer.h"

namespace csyren::render
{
    class ConstantBuffer
    {
    public:
        bool init(ID3D12Device* device, size_t size);
        void update(const void* data, size_t size);
        D3D12_GPU_VIRTUAL_ADDRESS gpuAddress() const {
            return _resource->GetGPUVirtualAddress();
        }
        void* cpuAddress() const { return _mapped; }

    private:
        Microsoft::WRL::ComPtr<ID3D12Resource> _resource;
        void* _mapped = nullptr;
        size_t _size = 0;
    };
}