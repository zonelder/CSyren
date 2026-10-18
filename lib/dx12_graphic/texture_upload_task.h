#pragma once
#include "upload_task.h"
#include "texture.h"

namespace csyren::render
{

    // Job-owned staging data; only onSync/onFailure access the manager.
	class TextureUploadTask : public UploadTaskBase
	{
	public:
		TextureUploadTask(TextureHandle handle, const std::string& name);

		void onUpload(UploadContext& thread) override;
		void onSync(ResourceManager& rm) override;
        bool isCurrent(const ResourceManager& rm) const override;
        void onFailure(ResourceManager& rm) override;
	private:
		bool					_loadFailed{ false };
		details::TextureBase	_dxData;
		const TextureHandle		_handle;
		const std::string		_name;
	};
}
