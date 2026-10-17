#include "pch.h"
#include "camera_render_target.h"
#include "renderer.h"
#include <stdexcept>

namespace csyren::render
{
    CameraRenderTarget::~CameraRenderTarget()
    {
        if (_queue) _queue->syncAwaitLastComplete();
        if (_descriptors) { _descriptors->freeSRV(_srv); _descriptors->freeRTV(_rtv); _descriptors->freeDSV(_dsv); }
    }
    void CameraRenderTarget::prepare(Renderer& renderer)
    { prepare(renderer.device(),*core::Services::get<DescriptorManager>(),renderer.queue()); }

    void CameraRenderTarget::prepare(ID3D12Device* device,DescriptorManager& manager,RenderQueue& queue)
    {
        if (_width == _requestedWidth && _height == _requestedHeight && _color) return;
        // Only called before this frame is recorded. Retire previous uses before
        // replacing resources or rewriting shader-visible descriptors.
        queue.syncAwaitLastComplete();
        _queue = &queue;
        auto* descriptors = &manager;
        D3D12_HEAP_PROPERTIES heap{}; heap.Type = D3D12_HEAP_TYPE_DEFAULT;
        D3D12_RESOURCE_DESC desc{};
        desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        desc.Width = _requestedWidth; desc.Height = _requestedHeight;
        desc.DepthOrArraySize = 1; desc.MipLevels = 1; desc.SampleDesc.Count = 1;
        desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM; desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
        details::ComPtr<ID3D12Resource> color,depth;
        if (FAILED(device->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&desc,
            D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,nullptr,IID_PPV_ARGS(&color))))
            throw std::runtime_error("Cannot allocate scene viewport color target");
        desc.Format = DXGI_FORMAT_D32_FLOAT; desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;
        D3D12_CLEAR_VALUE clear{}; clear.Format = desc.Format; clear.DepthStencil.Depth = 0;
        if (FAILED(device->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&desc,
            D3D12_RESOURCE_STATE_DEPTH_WRITE,&clear,IID_PPV_ARGS(&depth))))
            throw std::runtime_error("Cannot allocate scene viewport depth target");
        D3D12_SHADER_RESOURCE_VIEW_DESC srv{};
        srv.Format = DXGI_FORMAT_R8G8B8A8_UNORM; srv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING; srv.Texture2D.MipLevels = 1;
        D3D12_DEPTH_STENCIL_VIEW_DESC dsv{}; dsv.Format = DXGI_FORMAT_D32_FLOAT; dsv.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
        _descriptors = descriptors;
        if (_rtv) descriptors->recreateRTV(_rtv,color.Get()); else _rtv = descriptors->createRTV(color.Get());
        if (_dsv) descriptors->recreateDSV(_dsv,depth.Get(),&dsv); else _dsv = descriptors->createDSV(depth.Get(),&dsv);
        if (_srv) descriptors->recreateSRV(_srv,color.Get(),&srv); else _srv = descriptors->createSRV(color.Get(),&srv);
        _color = std::move(color); _depth = std::move(depth);
        _width = _requestedWidth; _height = _requestedHeight;
    }
}
