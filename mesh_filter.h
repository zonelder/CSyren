#ifndef __CSYREN_MESH_FILTER__
#define __CSYREN_MESH_FILTER__
#include <DirectXMath.h>
#include "core/renderer.h"
#include "dx12_graphic/serialize_base.h"

namespace csyren::render
{
    struct MeshFilter
    {
        render::MeshHandle mesh;
        static void describe();
    private:
        SERIALIZABLE(MeshFilter, mesh);
    };

    struct MeshRenderer
    {
        MaterialHandle material;
        static void describe();
    private:
        SERIALIZABLE(MeshRenderer, material);
    };
}

#endif
