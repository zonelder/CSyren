#ifndef __CSYREN_PRIMITIVE_MESH__
#define __CSYREN_PRIMITIVE_MESH__

namespace csyren::render
{
    class Renderer;
    class Mesh;
    class Material;

    class Primitives
    {
    public:
        static bool createDefaultMaterial(Renderer& renderer, Material& material);
        static bool createLine(Renderer& renderer, Mesh& mesh);
        static bool createTriangle(Renderer& renderer, Mesh& mesh);
    };
}

#endif