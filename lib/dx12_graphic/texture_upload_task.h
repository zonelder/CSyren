#pragma once
#include "upload_task.h"
#include "texture.h"

namespace csyren::render
{
	class TextureUploadTask : public UploadTaskBase
	{
	public:
		TextureUploadTask(TextureHandle handle, const std::string& name);

		void onUpload(UploadContext& thread) override;
		void onSync(ResourceManager& rm) override;
	private:
		Microsoft::WRL::ComPtr<ID3D12Resource> _textureResource;
		D3D12_RESOURCE_DESC _resourceDesc = {};
		bool _loadFailed = false;
		const TextureHandle _handle;
		const std::string _name;
	};
}
