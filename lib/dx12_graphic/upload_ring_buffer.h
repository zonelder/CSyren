#pragma once
#include "renderer.h"

namespace csyren::render
{
	class UploadRingBuffer
	{
	public:
        struct Allocation
        {
            size_t offset;
            size_t size;
        };

		UploadRingBuffer() noexcept = default;

        bool init(ID3D12Device* device, size_t frameBufferSize, UINT numFrames = 3);

        void beginFrame()
        {
            _currentFrame = (_currentFrame + 1) % _numFrames;
            _offset = 0;
        }
        size_t allocate(size_t size, void** cpuPtr, D3D12_GPU_VIRTUAL_ADDRESS* gpuAddr);


        size_t update(const void* data, size_t size, D3D12_GPU_VIRTUAL_ADDRESS* outGpuAddr);

        size_t currentOffset() const noexcept { return _offset; }

        details::ComPtr<ID3D12Resource> resource() const noexcept { return _buffer; }
    private:
        static size_t align256(size_t size)
        {
            return (size + 255) & ~255;
        }

        UINT _numFrames;
        UINT _currentFrame;
        size_t _frameSize;
        size_t _totalSize;
        size_t _offset;

        details::ComPtr<ID3D12Resource> _buffer;
        uint8_t* _mapped;
	};
}
