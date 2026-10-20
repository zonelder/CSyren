#pragma once
#include "descriptors.h"
#include <algorithm>
#include <array>
#include <memory>
#include <string_view>

namespace csyren::render
{
    class Renderer;
    class RenderQueue;

    // Per-camera attachments of the deferred pipeline. The frame graph names
    // them "view.<id>.<suffix>" (systems declare "scene.<suffix>"):
    //   color          final LDR image (tonemap output, editor/presentation input)
    //   depth          reverse-Z depth (R32_TYPELESS: DSV D32_FLOAT, SRV R32_FLOAT)
    //   gbuffer0       albedo.rgb + metallic      R8G8B8A8_UNORM
    //   gbuffer1       world normal.xyz + roughness R16G16B16A16_FLOAT
    //   gbuffer2       emissive.rgb               R11G11B10_FLOAT
    //   hdr            linear scene radiance      R16G16B16A16_FLOAT
    //   clusterGrid    light count per froxel     StructuredBuffer<uint>
    //   clusterIndices light indices per froxel   StructuredBuffer<uint>
    enum class ViewAttachment : uint8_t { Color, Depth, GBuffer0, GBuffer1, GBuffer2, Hdr, ClusterGrid, ClusterIndices, Count };

    struct ClusterGridLayout
    {
        static constexpr uint32_t X = 16, Y = 8, Z = 24;
        static constexpr uint32_t Count = X * Y * Z;
        static constexpr uint32_t MaxLightsPerCluster = 64;
        static constexpr uint32_t IndexCount = Count * MaxLightsPerCluster;
    };

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
        bool needsResize() const { return !_textures[0].resource || _width != _requestedWidth || _height != _requestedHeight; }

        // Legacy accessors (final color + depth).
        ID3D12Resource* color() const { return resource(ViewAttachment::Color); }
        ID3D12Resource* depth() const { return resource(ViewAttachment::Depth); }
        D3D12_CPU_DESCRIPTOR_HANDLE rtv() const { return rtv(ViewAttachment::Color); }
        D3D12_CPU_DESCRIPTOR_HANDLE dsv() const { return _textures[size_t(ViewAttachment::Depth)].rtvOrDsv.cpu; }
        D3D12_GPU_DESCRIPTOR_HANDLE srv() const { return srv(ViewAttachment::Color); }

        ID3D12Resource* resource(ViewAttachment attachment) const { return _textures[size_t(attachment)].resource.Get(); }
        D3D12_CPU_DESCRIPTOR_HANDLE rtv(ViewAttachment attachment) const { return _textures[size_t(attachment)].rtvOrDsv.cpu; }
        D3D12_GPU_DESCRIPTOR_HANDLE srv(ViewAttachment attachment) const { return _textures[size_t(attachment)].srv.gpu; }
        D3D12_GPU_VIRTUAL_ADDRESS address(ViewAttachment attachment) const
        { auto* r = resource(attachment); return r ? r->GetGPUVirtualAddress() : 0; }

        static const char* suffix(ViewAttachment attachment);
        static bool parse(std::string_view suffix, ViewAttachment& attachment);
        static bool isBuffer(ViewAttachment attachment) { return attachment >= ViewAttachment::ClusterGrid; }
        static bool isDepth(ViewAttachment attachment) { return attachment == ViewAttachment::Depth; }
        // Render target / DSV / SRV view format of the attachment.
        static DXGI_FORMAT targetFormat(ViewAttachment attachment);
        // State the attachment rests in between frames (frame graph initial/final state).
        static D3D12_RESOURCE_STATES restingState(ViewAttachment attachment);
    private:
        struct Attachment
        {
            details::ComPtr<ID3D12Resource> resource;
            DescriptorAllocation rtvOrDsv, srv;
        };
        void createTexture(ID3D12Device* device, ViewAttachment attachment);
        void createBuffer(ID3D12Device* device, ViewAttachment attachment, UINT64 bytes);
        std::array<Attachment, size_t(ViewAttachment::Count)> _textures;
        DescriptorManager* _descriptors = nullptr;
        RenderQueue* _queue = nullptr;
        UINT _width = 0,_height = 0,_requestedWidth = 640,_requestedHeight = 480;
    };
}
