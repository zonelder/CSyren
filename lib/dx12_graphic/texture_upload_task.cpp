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
        auto device = context.renderer()->device();
        auto renderer = context.renderer();
        if (_wcsicmp(extension.c_str(), L"dds") == 0)
        {
            hr = DirectX::CreateDDSTextureFromFile(
                device,
                context.batcher(),
                filePathW.c_str(),
                _dxData.buffer.ReleaseAndGetAddressOf()
            );
        }
        else
        {
            hr = DirectX::CreateWICTextureFromFile(
                device,
                context.batcher(),
                filePathW.c_str(),
                _dxData.buffer.ReleaseAndGetAddressOf()
            );
        }

        if (FAILED(hr))
        {
            log::error("TextureUploadTask: DDSTextureLoader/WICTextureLoader failed to load texture file.({}) HRESULT: 0x{:X}", _name, hr);
            _loadFailed = true;
            return;
        }  
        _dxData.desc = _dxData.buffer->GetDesc();
	}


	void TextureUploadTask::onSync(ResourceManager& rm)
	{
		//get texture from rm. check is this really exactly asked resource.

        if (_loadFailed)
        {
            log::error("TextureUploadTask: Failed to load texture {}", _name);
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
        srvDesc.Format = _dxData.desc.Format;
        srvDesc.Texture2D.MipLevels = _dxData.desc.MipLevels;
        srvDesc.Texture2D.MostDetailedMip = 0;
        srvDesc.Texture2D.ResourceMinLODClamp = 0.0f;

        // Simplified SRV dimension check
        if (_dxData.desc.Dimension == D3D12_RESOURCE_DIMENSION_TEXTURE2D)
        {
            srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        }
        else
        {
            srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        }
        // D3D12_CPU_DESCRIPTOR_HANDLE is only safe to use in the Main Thread
        renderer.device()->CreateShaderResourceView(_dxData.buffer.Get(), &srvDesc, srvHandles.cpuHandle);


        texture->_heapManager = heapManager;
        texture->_dxData = std::move(_dxData);
        texture->_srvHandles = std::move(srvHandles);
        texture->_loadStatus = LoadStatus::Loaded;

        log::debug("texture loading is complete {}", _name);
	}
}
