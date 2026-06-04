#ifndef __CSYREN_TEXTURE__
#define __CSYREN_TEXTURE__

#include "forward_decl.h"
#include "load_status.h"
#include "descriptors.h"

namespace csyren::render::details
{
    struct TextureBase
    {
        Microsoft::WRL::ComPtr<ID3D12Resource> buffer;
        D3D12_RESOURCE_DESC                    desc;
    };
}

namespace csyren::render
{
    class Texture
    {
        friend ResourceStorage<Texture>;
        friend class TextureUploadTask;
    public:
        D3D12_CPU_DESCRIPTOR_HANDLE getCpuSrvHandle() const;
        D3D12_GPU_DESCRIPTOR_HANDLE getGpuSrvHandle() const;

        Texture() noexcept;
        ~Texture();

        Texture(Texture&&) noexcept;
        Texture& operator=(Texture&&) noexcept;

        LoadStatus status() const noexcept { return _loadStatus; }
    private:

        Texture(const Texture&) = delete;
        Texture& operator=(const Texture&) = delete;

        bool init(Renderer& renderer, const std::wstring& filePath);

        bool init(Renderer& renderer, const std::string& filePath);
    private:
        details::TextureBase    _dxData;
        DescriptorAllocation    _srvHandles{};
        DescriptorManager*      _heapManager{ nullptr };
        LoadStatus              _loadStatus{ Loading };
    };
}


#endif
