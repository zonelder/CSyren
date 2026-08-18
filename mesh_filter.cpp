#include "mesh_filter.h"
#include "core/meta.h"

namespace csyren::render
{
    void MeshFilter::describe()
    {
        core::reflection::MetaFactory<MeshFilter>{}.type("MeshFilter"_hs);
    }
    void MeshRenderer::describe()
    {
        core::reflection::MetaFactory<MeshRenderer>{}.type("MeshRenderer"_hs);
    }

}
