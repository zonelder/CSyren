#pragma once
#include <d3d12.h>
#include <ResourceUploadBatch.h>
#include <wrl/client.h>

namespace csyren::render
{
    class Renderer;

    class UploadContext
    {
    public:
        UploadContext(ID3D12Device* device, Renderer* renderer)
            :
            _device(device),
            _renderer(renderer),
            _batcher(device)
        {
        }

        ID3D12Device* device() const noexcept { return _device.Get(); }

        DirectX::ResourceUploadBatch& batcher() noexcept { return _batcher; }
        Renderer* renderer() const noexcept { return _renderer; }

    private:
        Microsoft::WRL::ComPtr<ID3D12Device> _device;
        Renderer* _renderer;
        DirectX::ResourceUploadBatch _batcher;
    };
}
