#include "pch.h"
#include "upload_ring_buffer.h"

namespace csyren::render
{


    bool UploadRingBuffer::init(ID3D12Device* device, size_t frameBufferSize, UINT numFrames)
    {
        _numFrames = numFrames;
        _frameSize = align256(frameBufferSize);

        _buffers.resize(numFrames);
        _mapped.resize(numFrames);

        for (UINT i = 0; i < numFrames; i++)
        {
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
            resDesc.Width = _frameSize;
            resDesc.Height = 1;
            resDesc.DepthOrArraySize = 1;
            resDesc.MipLevels = 1;
            resDesc.Format = DXGI_FORMAT_UNKNOWN;
            resDesc.SampleDesc.Count = 1;
            resDesc.SampleDesc.Quality = 0;
            resDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
            resDesc.Flags = D3D12_RESOURCE_FLAG_NONE;

            if (FAILED(device->CreateCommittedResource(
                &heapProps,
                D3D12_HEAP_FLAG_NONE,
                &resDesc,
                D3D12_RESOURCE_STATE_GENERIC_READ,
                nullptr,
                IID_PPV_ARGS(&_buffers[i]))))
            {
                return false;
            }

            void* ptr = nullptr;
            D3D12_RANGE readRange{ 0, 0 };
            if (FAILED(_buffers[i]->Map(0, &readRange, &ptr)))
            {
                return false;
            }
            _mapped[i] = reinterpret_cast<uint8_t*>(ptr);
        }

        return true;
    }

    bool UploadRingBuffer::alloc(size_t size, void** cpuPtr, D3D12_GPU_VIRTUAL_ADDRESS* gpuAddr)
    {
        size = align256(size);
        if (_offset + size > _frameSize)
        {
            log::error("UploadRingBuffer: failed to allocate gpu memory : RUN OUT OF FRAME BUFFER MEMORY.");
            return false;
        }

        *cpuPtr = _mapped[_currentFrame] + _offset;
        *gpuAddr = _buffers[_currentFrame]->GetGPUVirtualAddress() + _offset;

        _offset += size;
        return true;
    }

    D3D12_GPU_VIRTUAL_ADDRESS UploadRingBuffer::update(const void* data, size_t size)
    {
        void* cpuPtr = nullptr;
        D3D12_GPU_VIRTUAL_ADDRESS gpuAddr = 0;

        if (!alloc(size, &cpuPtr, &gpuAddr))
        {
            log::error("UploadRingBuffer::update: failed to update buffer");
            return 0;
        }


        memcpy(cpuPtr, data, size);
        return gpuAddr;
    }

}
