#pragma once
#include "upload_task.h"
#include "texture.h"

namespace csyren::render
{

	/**
	 * @brief Task for uploading a texture resource.
	 *
	 * This class represents an upload task specifically for textures. It handles the process of uploading texture data
	 * from CPU to GPU memory and managing the synchronization of texture resources within the resource manager.
	 *
	 * The task performs the upload operation in a separate thread, and once completed, it synchronizes the resource
	 * with the resource manager, allowing it to be used by the rendering system.
	 *
	 * @note The task must be moved into the upload queue (using `addTask()`) and processed by the upload thread.
	 */
	class TextureUploadTask : public UploadTaskBase
	{
	public:
		TextureUploadTask(TextureHandle handle, const std::string& name);

		void onUpload(UploadContext& thread) override;
		void onSync(ResourceManager& rm) override;
	private:
		bool					_loadFailed{ false };
		details::TextureBase	_dxData;
		const TextureHandle		_handle;
		const std::string		_name;
	};
}
