#include "pch.h"
#include "texture_upload_task.h"
#include "upload_context.h"
#include "resource_manager.h"
#include "texture.h"
#include "cstdmf/string_utils.h"

#include <ResourceUploadBatch.h>
#include <DDSTextureLoader.h>
#include <WICTextureLoader.h>



namespace csyren::render
{
	TextureUploadTask::TextureUploadTask(TextureHandle handle, const std::string& name) :
		_handle(handle),
		_name(name)
	{

	}

	void TextureUploadTask::onUpload(UploadContext& context)
	{
        log::debug("TextureUploadTask: start upload of {}", _name);
		HRESULT hr = S_OK;
		std::wstring filePathW = cstdmf::to_wstring(_name);
		std::wstring extension = filePathW.substr(filePathW.find_last_of(L".") + 1);
        if (_wcsicmp(extension.c_str(), L"dds") == 0)
        {
            // Use DDSTextureLoader
            hr = DirectX::CreateDDSTextureFromFile(
                context.device(),
                context.batcher(),
                filePathW.c_str(),
                _textureResource.ReleaseAndGetAddressOf()
            );
        }
        else
        {
            // Use WICTextureLoader
            hr = DirectX::CreateWICTextureFromFile(
                context.device(),
                context.batcher(),
                filePathW.c_str(),
                _textureResource.ReleaseAndGetAddressOf()
            );
        }

        if (FAILED(hr))
        {
            log::error("TextureUploadTask: DDSTextureLoader/WICTextureLoader failed to load texture file.({}) HRESULT: 0x{:X}", _name, hr);
            _loadFailed = true;
            return;
        }

        // 2. ѕолучаем описание ресурса дл€ создани€ SRV в onSync
        _resourceDesc = _textureResource->GetDesc();
	}


	void TextureUploadTask::onSync(ResourceManager& rm)
	{
		//get texture from rm. check is this really exactly asked resource.

        if (_loadFailed)
        {
            rm.unload(_handle);
            return;
        }

        if (rm.getTextureName(_handle) != _name)
        {
            log::error("TextureUploadTask::Cant sync texture {},erase it from manager.", _name);
            rm.unload(_handle);
            return;
        }
        Texture* texture = rm.getTexture(_handle);
        Renderer& renderer = rm.renderer();

        auto heapManager = renderer.getDescriptorHeapManager();

        if (!heapManager)
        {
            log::error("TextureUploadTask: cant get heap manager for sync texture.");
            rm.unload(_handle);
            return;
        }

        auto srvHandles = heapManager->allocate();

        if (!srvHandles.isValid())
        {
            log::error("TextureUploadTask: failed to allocate desctiptor for texture.");
            rm.unload(_handle);
            return;
        }

        D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
        srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        srvDesc.Format = _resourceDesc.Format;
        srvDesc.Texture2D.MipLevels = _resourceDesc.MipLevels;
        srvDesc.Texture2D.MostDetailedMip = 0;
        srvDesc.Texture2D.ResourceMinLODClamp = 0.0f;

        // Simplified SRV dimension check
        if (_resourceDesc.Dimension == D3D12_RESOURCE_DIMENSION_TEXTURE2D)
        {
            srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        }
        else
        {
            srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        }
        D3D12_RESOURCE_BARRIER barrier = {};
        barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
        barrier.Transition.pResource = _textureResource.Get();
        barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
        barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
        barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;

        renderer.commandList()->ResourceBarrier(1, &barrier);

        // D3D12_CPU_DESCRIPTOR_HANDLE is only safe to use in the Main Thread
        renderer.device()->CreateShaderResourceView(_textureResource.Get(), &srvDesc, srvHandles.cpuHandle);


        texture->_heapManager = heapManager;
        texture->_format = _resourceDesc.Format;
        texture->_height = _resourceDesc.Height;
        texture->_width = _resourceDesc.Width;
        texture->_mipmapCount = _resourceDesc.MipLevels;
        texture->_textureResource = std::move(_textureResource);
        texture->_srvHandles = std::move(srvHandles);
        texture->_loadStatus = LoadStatus::Loaded;

        log::debug("texture loading is complete,{}", _name);
	}
}
