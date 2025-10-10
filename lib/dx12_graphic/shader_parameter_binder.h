#pragma once

// ShaderParameterBinder.h
#pragma once

#include <unordered_map>
#include <memory>
#include "resource_handle.h"
#include "constant_buffer.h"
#include "resource_manager.h"
#include "upload_ring_buffer.h"


namespace csyren::render::details
{
    class GpuBuffer
    {
    public:
        GpuBuffer() = default;

        // Универсальный init для создания буфера в DEFAULT куче
        bool init(ID3D12Device* device, size_t size, D3D12_RESOURCE_STATES initialState = D3D12_RESOURCE_STATE_COMMON)
        {
            // Если буфер уже существует, освобождаем его перед созданием нового
            if (_resource)
            {
                _resource.Reset();
            }
            _size = size;
            if (_size == 0) return true; // Можно создать "пустой" буфер

            D3D12_HEAP_PROPERTIES heapProps = {};
            heapProps.Type = D3D12_HEAP_TYPE_DEFAULT;
            heapProps.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
            heapProps.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
            heapProps.CreationNodeMask = 1;
            heapProps.VisibleNodeMask = 1;

            D3D12_RESOURCE_DESC resDesc = {};
            resDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
            resDesc.Alignment = 0;
            resDesc.Width = _size;
            resDesc.Height = 1;
            resDesc.DepthOrArraySize = 1;
            resDesc.MipLevels = 1;
            resDesc.Format = DXGI_FORMAT_UNKNOWN;
            resDesc.SampleDesc.Count = 1;
            resDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
            resDesc.Flags = D3D12_RESOURCE_FLAG_NONE;

            if (FAILED(device->CreateCommittedResource(&heapProps, D3D12_HEAP_FLAG_NONE, &resDesc, initialState, nullptr, IID_PPV_ARGS(&_resource))))
            {
                _size = 0;
                return false;
            }
            return true;
        }

        ID3D12Resource* getResource() const { return _resource.Get(); }
        D3D12_GPU_VIRTUAL_ADDRESS getGpuAddress() const { return _resource ? _resource->GetGPUVirtualAddress() : 0; }
        size_t size() const { return _size; }

        // Разрешаем перемещение, запрещаем копирование
        GpuBuffer(GpuBuffer&&) noexcept = default;
        GpuBuffer& operator=(GpuBuffer&&) noexcept = default;
        GpuBuffer(const GpuBuffer&) = delete;
        GpuBuffer& operator=(const GpuBuffer&) = delete;

    private:
        Microsoft::WRL::ComPtr<ID3D12Resource> _resource;
        size_t _size = 0;
    };
}

namespace csyren::render
{
    class Renderer;
    struct EngineVariableBuffer;
    class ShaderParameterBinder
    {
        using BufferHash = size_t;
    public:
        void beginFrame(uint64_t frameNumber);

        bool updateMaterialBuffer(ID3D12Device* device, ID3D12GraphicsCommandList* cmdList, ResourceManager* rm, UploadRingBuffer& ringBuffer, MaterialHandle material);

        bool updateFrameBuffer(ID3D12Device* device, ID3D12GraphicsCommandList* cmdList,EngineVariableBuffer& buffer, UploadRingBuffer& ringBuffer, Shader* shader);
    private:
        struct MaterialBufferCache
        {
            details::GpuBuffer  buffer;
            uint64_t lastUpdatedVersion = (uint64_t)(- 1);
        };
        struct FrameBufferCache
        {
            D3D12_GPU_VIRTUAL_ADDRESS gpuAddress = 0;
        };

    private:
        std::unordered_map<MaterialHandle, MaterialBufferCache> _materialCache;
        std::unordered_map<BufferHash, FrameBufferCache>         _frameCache;
    };
}
