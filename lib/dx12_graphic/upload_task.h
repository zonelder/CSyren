#pragma once

#include "forward_decl.h"

#include <memory>

namespace csyren::render
{
	class UploadTaskBase
	{
	public:
		using Ptr = std::unique_ptr<UploadTaskBase>;
		virtual void onUpload(UploadContext& thread) = 0;
		virtual void onSync(ResourceManager& rm) = 0;
	};
}
