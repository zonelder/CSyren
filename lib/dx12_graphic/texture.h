#ifndef __CSYREN_TEXTURE__
#define __CSYREN_TEXTURE__


#include <filesystem>
#include "renderer.h"

namespace csyren::render
{

    template<typename T> class ResourceStorage;

    class Texture
    {
        friend class ResourceStorage<Texture>;
    public:
        D3D12_CPU_DESCRIPTOR_HANDLE getCpuSrvHandle() const;
        D3D12_GPU_DESCRIPTOR_HANDLE getGpuSrvHandle() const;

        Texture(DescriptorHeapManager* manager);
        ~Texture();
    private:

        Texture(const Texture&) = delete;
        Texture& operator=(const Texture&) = delete;

        Texture(Texture&&) noexcept;
        Texture& operator=(Texture&&) noexcept;

        bool init(Renderer& renderer, const std::wstring& filePath);
    private:
        Microsoft::WRL::ComPtr<ID3D12Resource> _textureResource;
        DescriptorHandles _srvHandles{};
        DescriptorHeapManager* _heapManager{ nullptr };
        UINT _mipmapCount{ 0 }; // Сохранять при создании
        UINT _width{ 0 };
        UINT _height{ 0 };
        DXGI_FORMAT _format{ DXGI_FORMAT_UNKNOWN };
    };
}


#endif
