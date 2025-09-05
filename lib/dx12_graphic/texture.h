#ifndef __CSYREN_TEXTURE__
#define __CSYREN_TEXTURE__


#include <filesystem>
#include "renderer.h"

namespace csyren::render
{
    class Renderer;
    template<typename T> class ResourceStorage;

    class Texture
    {
        friend class ResourceStorage<Texture>;
    public:
        D3D12_CPU_DESCRIPTOR_HANDLE getCpuSrvHandle() const;
        D3D12_GPU_DESCRIPTOR_HANDLE getGpuSrvHandle() const;

        Texture() noexcept;
        ~Texture();

        Texture(Texture&&) noexcept;
        Texture& operator=(Texture&&) noexcept;
    private:

        Texture(const Texture&) = delete;
        Texture& operator=(const Texture&) = delete;



        bool init(Renderer& renderer, const std::wstring& filePath);

        bool init(Renderer& renderer, const std::string& filePath);
    private:
        Microsoft::WRL::ComPtr<ID3D12Resource> _textureResource;
        DescriptorHandles _srvHandles{};
        DescriptorHeapManager* _heapManager{ nullptr };
        UINT _mipmapCount{ 0 };
        UINT _width{ 0 };
        UINT _height{ 0 };
        DXGI_FORMAT _format{ DXGI_FORMAT_UNKNOWN };
    };
}


#endif
