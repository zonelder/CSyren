#ifndef __CSYREN_MESH_FILTER__
#define __CSYREN_MESH_FILTER__
#include <DirectXMath.h>
#include "core/renderer.h"

namespace csyren::components
{
    struct MeshFilter
    {
        render::MeshHandle mesh;
    };

    struct MeshRenderer
    {
        csyren::render::MaterialHandle material;
    };
}

#endif
