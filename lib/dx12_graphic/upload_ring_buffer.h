#pragma once
#include "renderer.h"

namespace csyren::render
{
	class UploadRingBuffer
	{
	public:
		UploadRingBuffer() = default;

        bool init(ID3D12Device* device, size_t frameBufferSize, UINT numFrames = 3);

        void beginFrame()
        {
            _currentFrame = (_currentFrame + 1) % _numFrames;
            _offset = 0;
        }
        struct Allocation
        {
            size_t offset;
            size_t size;
        };
        size_t alloc(size_t size, void** cpuPtr, D3D12_GPU_VIRTUAL_ADDRESS* gpuAddr);


        size_t update(const void* data, size_t size, D3D12_GPU_VIRTUAL_ADDRESS* outGpuAddr);

        size_t currentOffset() const noexcept { return _offset; }

        Microsoft::WRL::ComPtr<ID3D12Resource> currentResource() const noexcept { return _buffers[_currentFrame]; }
    private:
        static size_t align256(size_t size)
        {
            return (size + 255) & ~255;
        }

        UINT _numFrames = 0;
        UINT _currentFrame = 0;
        size_t _frameSize = 0;
        size_t _offset = 0;

        std::vector<Microsoft::WRL::ComPtr<ID3D12Resource>> _buffers;
        std::vector<uint8_t*> _mapped;
	};
}
