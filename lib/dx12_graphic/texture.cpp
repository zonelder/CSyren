#include "pch.h"
#include "texture.h"
#include "cstdmf/string_utils.h"
#include "renderer.h"

#include <DirectXTex.h>
#include <DDSTextureLoader.h>
#include <WICTextureLoader.h>
#include <ResourceUploadBatch.h>

namespace csyren::render
{
    Texture::Texture() noexcept : _heapManager(nullptr) {}

    Texture::~Texture()
    {
        if (_heapManager && _srvHandles.valid())
        {
            _heapManager->freeSRV(_srvHandles);
        }
    }

    Texture::Texture(Texture&& other) noexcept
        : _dxData(std::move(other._dxData)),
        _srvHandles(std::move(other._srvHandles)),
        _heapManager(other._heapManager)
    {
        other._srvHandles = {};
        other._heapManager = nullptr;
    }

    Texture& Texture::operator=(Texture&& other) noexcept
    {
        if (this != &other)
        {
            if (_heapManager && _srvHandles.valid()) { _heapManager->freeSRV(_srvHandles); }

            _dxData = std::move(other._dxData);
            _srvHandles = std::move(other._srvHandles);
            _heapManager = other._heapManager;

            other._srvHandles = {};
            other._heapManager = nullptr;
        }
        return *this;
    }

    bool Texture::init(Renderer& r, const std::string& filePath)
    {
        return init(r,cstdmf::to_wstring(filePath));
    }

    bool Texture::init(Renderer& renderer, const std::wstring& filePath)
    {
        if (!std::filesystem::exists(filePath))
        {
            log::warning("Texture: texture file is not found.({})", cstdmf::to_string(filePath));
            return false;
        }
        //do nothing as resource manager pass this texture to upload thread and its been loaded there.
        return true;
    }

    D3D12_CPU_DESCRIPTOR_HANDLE Texture::getCpuSrvHandle() const
    {
        return _srvHandles.cpu;
    }

    D3D12_GPU_DESCRIPTOR_HANDLE Texture::getGpuSrvHandle() const
    {
        return _srvHandles.gpu;
    }
}
