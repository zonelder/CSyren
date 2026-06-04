#pragma once
#include "forward_decl.h"

#include <d3d12.h>
#include <ResourceUploadBatch.h>

namespace csyren::render
{
    class UploadContext
    {
    public:
        UploadContext(Renderer* renderer);
        DirectX::ResourceUploadBatch& batcher() noexcept { return _batcher; }
    private:
        Renderer* _renderer;
        DirectX::ResourceUploadBatch _batcher;
    };
}
