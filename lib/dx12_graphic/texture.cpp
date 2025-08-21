#include "pch.h"
#include "texture.h"
#include "renderer.h"
#include <DirectXTex.h>

namespace csyren::render
{
    Texture::Texture(DescriptorHeapManager* manager) : _heapManager(manager) {}

    Texture::~Texture()
    {
        if (_heapManager && _srvHandles.isValid())
        {
            _heapManager->free(_srvHandles);
        }
    }

    Texture::Texture(Texture&& other) noexcept
        : _textureResource(std::move(other._textureResource)),
        _srvHandles(other._srvHandles),
        _heapManager(other._heapManager)
    {
        other._srvHandles = {};
        other._heapManager = nullptr;
    }

    Texture& Texture::operator=(Texture&& other) noexcept
    {
        if (this != &other)
        {
            if (_heapManager && _srvHandles.isValid()) { _heapManager->free(_srvHandles); }

            _textureResource = std::move(other._textureResource);
            _srvHandles = other._srvHandles;
            _heapManager = other._heapManager;

            other._srvHandles = {};
            other._heapManager = nullptr;
        }
        return *this;
    }

    bool Texture::init(Renderer& renderer, const std::wstring& filePath)
    {
        if (!std::filesystem::exists(filePath))
        {
            //log::warning("Texture: texture file is not found.({})", filePath);
            return false;
        }

        DirectX::TexMetadata metadata;
        DirectX::ScratchImage scratchImage;


        HRESULT hr;
        std::wstring extension = filePath.substr(filePath.find_last_of(L".") + 1);

        if (_wcsicmp(extension.c_str(), L"dds") == 0)
        {
            hr = DirectX::LoadFromDDSFile(filePath.c_str(), DirectX::DDS_FLAGS_NONE, &metadata, scratchImage);
        }
        else
        {
            hr = DirectX::LoadFromWICFile(filePath.c_str(), DirectX::WIC_FLAGS_NONE, &metadata, scratchImage);
        }

        if (FAILED(hr))
        {
            //log::error("Texture: cant load\\decode texture file.({})", filePath);
            return false;
        }

        hr = DirectX::CreateTexture(renderer.device(), metadata, &_textureResource);
        if (FAILED(hr))
        {
            //log::error("Texture: cant  create GPU resource({})", filePath);
            return false;
        }
        _textureResource->SetName((L"Texture: " + filePath).c_str());

        hr = renderer.uploadTextureData(_textureResource.Get(), scratchImage);
        if (FAILED(hr))
        {
            //log::error("Texture: cant copy texture data to GPU({})", filePath);
            return false;
        }

        _srvHandles = _heapManager->allocate();
        if (!_srvHandles.isValid())
        {
            //log::error("Texture: failed to allocate descriptor.");
            return false;
        }

        D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
        srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        srvDesc.Format = metadata.format;
        srvDesc.ViewDimension = static_cast<D3D12_SRV_DIMENSION>(metadata.dimension);
        srvDesc.Texture2D.MipLevels = static_cast<UINT>(metadata.mipLevels);
        srvDesc.Texture2D.MostDetailedMip = 0;
        srvDesc.Texture2D.ResourceMinLODClamp = 0.0f;

        renderer.device()->CreateShaderResourceView(_textureResource.Get(), &srvDesc, _srvHandles.cpuHandle);

        return true;
    }

    D3D12_CPU_DESCRIPTOR_HANDLE Texture::getCpuSrvHandle() const
    {
        return _srvHandles.cpuHandle;
    }

    D3D12_GPU_DESCRIPTOR_HANDLE Texture::getGpuSrvHandle() const
    {
        return _srvHandles.gpuHandle;
    }
}
