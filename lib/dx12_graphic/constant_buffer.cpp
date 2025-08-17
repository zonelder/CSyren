#include "pch.h"
#include "constant_buffer.h"


namespace csyren::render
{
   bool ConstantBuffer::init(ID3D12Device* device, size_t size)
    {
        _size = size;
        
        // Align size to 256 bytes (constant buffer requirement)
        const size_t alignedSize = (size + 255) & ~255;
        
        // Create upload heap properties
        D3D12_HEAP_PROPERTIES heapProps = {};
        heapProps.Type = D3D12_HEAP_TYPE_UPLOAD;
        heapProps.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
        heapProps.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
        heapProps.CreationNodeMask = 1;
        heapProps.VisibleNodeMask = 1;
        
        // Create resource descriptor
        D3D12_RESOURCE_DESC resDesc = {};
        resDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        resDesc.Alignment = 0;
        resDesc.Width = alignedSize;
        resDesc.Height = 1;
        resDesc.DepthOrArraySize = 1;
        resDesc.MipLevels = 1;
        resDesc.Format = DXGI_FORMAT_UNKNOWN;
        resDesc.SampleDesc.Count = 1;
        resDesc.SampleDesc.Quality = 0;
        resDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        resDesc.Flags = D3D12_RESOURCE_FLAG_NONE;
        
        // Create committed resource
        if (DX_FAILED(device->CreateCommittedResource(
            &heapProps,
            D3D12_HEAP_FLAG_NONE,
            &resDesc,
            D3D12_RESOURCE_STATE_GENERIC_READ,
            nullptr,
            IID_PPV_ARGS(&_resource))))
        {
            return false;
        }

        // Map and keep persistent pointer
        D3D12_RANGE readRange{0, 0};
        if (DX_FAILED(_resource->Map(0, &readRange, &_mapped)))
        {
            return false;
        }
        
        return true;
    }

    void ConstantBuffer::update(const void* data, size_t size)
    {
        if (size > _size) return;
        memcpy(_mapped, data, size);
    }

}