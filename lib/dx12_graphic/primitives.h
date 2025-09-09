#ifndef __CSYREN_PRIMITIVE_MESH__
#define __CSYREN_PRIMITIVE_MESH__

#include "resource_manager.h"

namespace csyren::render
{
    class Renderer;
    class Mesh;
    class Material;

    class Primitives
    {
    public:
        static ShaderHandle getDefaultShader(ResourceManager& rm);
        static MaterialHandle getDefaultMaterial(ResourceManager& rm);
        static MeshHandle getLine(ResourceManager& rm);
        static MeshHandle getTriangle(ResourceManager& rm);
        static MeshHandle getQuad(ResourceManager& rm);
        static MeshHandle getCube(ResourceManager& rm);
    private:
        friend class Application;
        static bool registerFabricsAll(ResourceManager&);
    };
}

#endif