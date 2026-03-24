#include "pch.h"
#include "renderer.h"


namespace csyren::render
{
    UploadContext::UploadContext(Renderer* renderer)
        :
        _renderer(renderer),
        _batcher(renderer->device())
    {
    }
}