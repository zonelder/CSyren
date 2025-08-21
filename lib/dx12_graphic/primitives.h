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

        static MaterialHandle getHardcodedMaterial(ResourceManager& rm);
        static ShaderHandle getDefaultShader(ResourceManager& rm);
        static MaterialHandle getDefaultMaterial(ResourceManager& rm);
        static MeshHandle getLine(ResourceManager& rm);
        static MeshHandle getTriangle(ResourceManager& rm);
        static MeshHandle getQuad(ResourceManager& rm);
        static MeshHandle getCube(ResourceManager& rm);

        static void clearCache();

    private:
        inline static MaterialHandle s_hardcodedMaterial;
        inline static ShaderHandle   s_defaultShader{};
        inline static MaterialHandle s_defaultMaterial{};
        inline static MeshHandle s_lineMesh{};
        inline static MeshHandle s_triangleMesh{};

        inline static MeshHandle s_quadMesh{};
        inline static MeshHandle s_cubeMesh{};
        
    };
}

#endif