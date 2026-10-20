#ifndef __CSYREN_MESH_FILTER__
#define __CSYREN_MESH_FILTER__
#include <DirectXMath.h>
#include "dx12_graphic/resource_handle.h"

namespace csyren::render
{
    struct MeshFilter
    {
        render::MeshHandle mesh;
        static void describe();
    };

    struct MeshRenderer
    {
        MaterialHandle material;
        static void describe();
    };
}

#endif
