#pragma once
#include "descriptors.h"
#include <algorithm>
#include <memory>

namespace csyren::render
{
    class Renderer;
    class RenderQueue;
    // Transient camera output, independent of the final presentation frame.
    // Owned by the renderer's camera registry; never stored in scene components.
    class CameraRenderTarget
    {
    public:
        ~CameraRenderTarget();
        void request(UINT width, UINT height)
        { _requestedWidth = std::clamp(width,1u,8192u); _requestedHeight = std::clamp(height,1u,8192u); }
        void prepare(Renderer& renderer);
        void prepare(ID3D12Device* device,DescriptorManager& descriptors,RenderQueue& queue);
        UINT width() const { return _width; }
        UINT height() const { return _height; }
        bool needsResize() const { return !_color || _width != _requestedWidth || _height != _requestedHeight; }
        ID3D12Resource* color() const { return _color.Get(); }
        ID3D12Resource* depth() const { return _depth.Get(); }
        D3D12_CPU_DESCRIPTOR_HANDLE rtv() const { return _rtv.cpu; }
        D3D12_CPU_DESCRIPTOR_HANDLE dsv() const { return _dsv.cpu; }
        D3D12_GPU_DESCRIPTOR_HANDLE srv() const { return _srv.gpu; }
    private:
        details::ComPtr<ID3D12Resource> _color,_depth;
        DescriptorAllocation _rtv,_dsv,_srv;
        DescriptorManager* _descriptors = nullptr;
        RenderQueue* _queue = nullptr;
        UINT _width = 0,_height = 0,_requestedWidth = 640,_requestedHeight = 480;
    };
}
