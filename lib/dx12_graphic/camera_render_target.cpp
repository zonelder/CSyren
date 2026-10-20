#include "pch.h"
#include "camera_render_target.h"
#include "renderer.h"
#include <stdexcept>

namespace csyren::render
{
    namespace
    {
        constexpr const char* kSuffixes[size_t(ViewAttachment::Count)] = {
            "color", "depth", "gbuffer0", "gbuffer1", "gbuffer2", "hdr", "clusterGrid", "clusterIndices"
        };
        DXGI_FORMAT resourceFormat(ViewAttachment attachment)
        {
            switch (attachment)
            {
            case ViewAttachment::Depth: return DXGI_FORMAT_R32_TYPELESS;
            default: return CameraRenderTarget::targetFormat(attachment);
            }
        }
    }

    const char* CameraRenderTarget::suffix(ViewAttachment attachment) { return kSuffixes[size_t(attachment)]; }

    bool CameraRenderTarget::parse(std::string_view suffix, ViewAttachment& attachment)
    {
        for (size_t i = 0; i < size_t(ViewAttachment::Count); ++i)
            if (suffix == kSuffixes[i]) { attachment = ViewAttachment(i); return true; }
        return false;
    }

    DXGI_FORMAT CameraRenderTarget::targetFormat(ViewAttachment attachment)
    {
        switch (attachment)
        {
        case ViewAttachment::Color: return DXGI_FORMAT_R8G8B8A8_UNORM;
        case ViewAttachment::Depth: return DXGI_FORMAT_D32_FLOAT;
        case ViewAttachment::GBuffer0: return DXGI_FORMAT_R8G8B8A8_UNORM;
        case ViewAttachment::GBuffer1: return DXGI_FORMAT_R16G16B16A16_FLOAT;
        case ViewAttachment::GBuffer2: return DXGI_FORMAT_R11G11B10_FLOAT;
        case ViewAttachment::Hdr: return DXGI_FORMAT_R16G16B16A16_FLOAT;
        default: return DXGI_FORMAT_UNKNOWN;
        }
    }

    D3D12_RESOURCE_STATES CameraRenderTarget::restingState(ViewAttachment attachment)
    {
        switch (attachment)
        {
        case ViewAttachment::Depth: return D3D12_RESOURCE_STATE_DEPTH_WRITE;
        default: return D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
        }
    }

    CameraRenderTarget::~CameraRenderTarget()
    {
        if (_queue) _queue->syncAwaitLastComplete();
        if (_descriptors)
        {
            for (size_t i = 0; i < _textures.size(); ++i)
            {
                auto& a = _textures[i];
                _descriptors->freeSRV(a.srv);
                if (isDepth(ViewAttachment(i))) _descriptors->freeDSV(a.rtvOrDsv);
                else if (!isBuffer(ViewAttachment(i))) _descriptors->freeRTV(a.rtvOrDsv);
            }
        }
    }
    void CameraRenderTarget::prepare(Renderer& renderer)
    { prepare(renderer.device(),*core::Services::get<DescriptorManager>(),renderer.queue()); }

    void CameraRenderTarget::createTexture(ID3D12Device* device, ViewAttachment attachment)
    {
        auto& slot = _textures[size_t(attachment)];
        D3D12_HEAP_PROPERTIES heap{}; heap.Type = D3D12_HEAP_TYPE_DEFAULT;
        D3D12_RESOURCE_DESC desc{};
        desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        desc.Width = _requestedWidth; desc.Height = _requestedHeight;
        desc.DepthOrArraySize = 1; desc.MipLevels = 1; desc.SampleDesc.Count = 1;
        desc.Format = resourceFormat(attachment);
        const bool depth = isDepth(attachment);
        desc.Flags = depth ? D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL : D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
        D3D12_CLEAR_VALUE clear{}; clear.Format = targetFormat(attachment);
        if (depth) clear.DepthStencil.Depth = 0; // reverse-Z: far plane = 0
        details::ComPtr<ID3D12Resource> resource;
        if (FAILED(device->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&desc,
            restingState(attachment),&clear,IID_PPV_ARGS(&resource))))
            throw std::runtime_error(std::string("Cannot allocate camera attachment ") + suffix(attachment));
        D3D12_SHADER_RESOURCE_VIEW_DESC srv{};
        srv.Format = depth ? DXGI_FORMAT_R32_FLOAT : targetFormat(attachment);
        srv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING; srv.Texture2D.MipLevels = 1;
        if (slot.srv) _descriptors->recreateSRV(slot.srv,resource.Get(),&srv); else slot.srv = _descriptors->createSRV(resource.Get(),&srv);
        if (depth)
        {
            D3D12_DEPTH_STENCIL_VIEW_DESC dsv{}; dsv.Format = DXGI_FORMAT_D32_FLOAT; dsv.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
            if (slot.rtvOrDsv) _descriptors->recreateDSV(slot.rtvOrDsv,resource.Get(),&dsv); else slot.rtvOrDsv = _descriptors->createDSV(resource.Get(),&dsv);
        }
        else
        {
            if (slot.rtvOrDsv) _descriptors->recreateRTV(slot.rtvOrDsv,resource.Get()); else slot.rtvOrDsv = _descriptors->createRTV(resource.Get());
        }
        slot.resource = std::move(resource);
    }

    void CameraRenderTarget::createBuffer(ID3D12Device* device, ViewAttachment attachment, UINT64 bytes)
    {
        auto& slot = _textures[size_t(attachment)];
        if (slot.resource) return; // fixed size: survives resizes
        D3D12_HEAP_PROPERTIES heap{}; heap.Type = D3D12_HEAP_TYPE_DEFAULT;
        D3D12_RESOURCE_DESC desc{};
        desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        desc.Width = bytes; desc.Height = 1; desc.DepthOrArraySize = 1; desc.MipLevels = 1;
        desc.Format = DXGI_FORMAT_UNKNOWN; desc.SampleDesc.Count = 1;
        desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
        details::ComPtr<ID3D12Resource> resource;
        if (FAILED(device->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&desc,
            restingState(attachment),nullptr,IID_PPV_ARGS(&resource))))
            throw std::runtime_error(std::string("Cannot allocate camera buffer ") + suffix(attachment));
        slot.resource = std::move(resource);
    }

    void CameraRenderTarget::prepare(ID3D12Device* device,DescriptorManager& manager,RenderQueue& queue)
    {
        if (_width == _requestedWidth && _height == _requestedHeight && _textures[0].resource) return;
        // Only called before this frame is recorded. Retire previous uses before
        // replacing resources or rewriting shader-visible descriptors.
        queue.syncAwaitLastComplete();
        _queue = &queue;
        _descriptors = &manager;
        for (size_t i = 0; i < _textures.size(); ++i)
        {
            const auto attachment = ViewAttachment(i);
            if (isBuffer(attachment)) continue;
            createTexture(device, attachment);
        }
        createBuffer(device, ViewAttachment::ClusterGrid, UINT64(ClusterGridLayout::Count) * sizeof(uint32_t));
        createBuffer(device, ViewAttachment::ClusterIndices, UINT64(ClusterGridLayout::IndexCount) * sizeof(uint32_t));
        _width = _requestedWidth; _height = _requestedHeight;
    }
}
