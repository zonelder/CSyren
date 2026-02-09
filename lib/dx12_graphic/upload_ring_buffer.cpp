#include "pch.h"
#include "upload_ring_buffer.h"

namespace csyren::render
{
    bool UploadRingBuffer::init(ID3D12Device* device, size_t frameBufferSize, UINT numFrames)
    {
        _numFrames = numFrames;
        _frameSize = align256(frameBufferSize);

        // --- heap props ---
        D3D12_HEAP_PROPERTIES heapProps = {};
        heapProps.Type = D3D12_HEAP_TYPE_UPLOAD;
        heapProps.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
        heapProps.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
        heapProps.CreationNodeMask = 1;
        heapProps.VisibleNodeMask = 1;

        // --- resource desc ---
        D3D12_RESOURCE_DESC resDesc = {};
        resDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        resDesc.Alignment = 0;
        resDesc.Width = _frameSize*numFrames;
        resDesc.Height = 1;
        resDesc.DepthOrArraySize = 1;
        resDesc.MipLevels = 1;
        resDesc.Format = DXGI_FORMAT_UNKNOWN;
        resDesc.SampleDesc.Count = 1;
        resDesc.SampleDesc.Quality = 0;
        resDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        resDesc.Flags = D3D12_RESOURCE_FLAG_NONE;

        if (DX_FAILED(device->CreateCommittedResource(
            &heapProps,
            D3D12_HEAP_FLAG_NONE,
            &resDesc,
            D3D12_RESOURCE_STATE_GENERIC_READ,
            nullptr,
            IID_PPV_ARGS(&_buffer))))
        {
            return false;
        }

        void* ptr = nullptr;
        D3D12_RANGE readRange{ 0, 0 };
        if (DX_FAILED(_buffer->Map(0, &readRange, &ptr)))
        {
            return false;
        }
        _mapped = reinterpret_cast<uint8_t*>(ptr);
        return true;
    }

    size_t UploadRingBuffer::allocate(size_t size, void** cpuPtr, D3D12_GPU_VIRTUAL_ADDRESS* gpuAddr)
    {
        size = align256(size);
        if (_offset + size > _frameSize)
        {
            log::error("UploadRingBuffer: failed to allocate gpu memory : RUN OUT OF FRAME BUFFER MEMORY.");
            return -1;
        }
        size_t frameBase = _currentFrame * _frameSize;
        size_t totalOffset = frameBase + _offset;
        *cpuPtr = _mapped + totalOffset;
        *gpuAddr = _buffer->GetGPUVirtualAddress() + totalOffset;
        _offset += size;
        return totalOffset;
    }

    size_t UploadRingBuffer::update(const void* data, size_t size, D3D12_GPU_VIRTUAL_ADDRESS* outGpuAddr)
    {
        void* cpuPtr = nullptr;
        D3D12_GPU_VIRTUAL_ADDRESS gpuAddr = 0;

        size_t offset = allocate(size, &cpuPtr, &gpuAddr);
        if (offset == -1)
        {
            log::error("UploadRingBuffer::update: failed to update buffer");
            if (outGpuAddr) *outGpuAddr = 0;
            return -1;
        }


        memcpy(cpuPtr, data, size);
        if (outGpuAddr) *outGpuAddr = gpuAddr;
        return offset;
    }

}
