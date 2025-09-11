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

        bool alloc(size_t size, void** cpuPtr, D3D12_GPU_VIRTUAL_ADDRESS* gpuAddr);

        D3D12_GPU_VIRTUAL_ADDRESS update(const void* data, size_t size);

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
