#ifndef __CSYREN_MESH_FILTER__
#define __CSYREN_MESH_FILTER__
#include <DirectXMath.h>
#include "core/renderer.h"
#include "core/serializer.h"

namespace csyren::render::components
{
    struct MeshFilter
    {
        render::MeshHandle mesh;
    private:
        SERIALIZABLE(MeshFilter, mesh);
    };

    struct MeshRenderer
    {
        csyren::render::MaterialHandle material;
    private:
        SERIALIZABLE(MeshRenderer, material);
    };
}

#endif
