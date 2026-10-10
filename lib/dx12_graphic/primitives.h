#ifndef __CSYREN_PRIMITIVE_MESH__
#define __CSYREN_PRIMITIVE_MESH__

#include "resource_manager.h"
#include "material.h"

namespace csyren::render
{
    class Primitives
    {
    public:
        static ShaderHandle getDefaultShader();
        static MaterialHandle getDefaultMaterial();
        static MeshHandle getLine();
        static MeshHandle getTriangle();
        static MeshHandle getQuad();
        static MeshHandle getCube();
        static MeshHandle getSphere();
        static ShaderHandle getRainbowShader();
        static MaterialHandle getRainbowMaterial();


        static ShaderHandle getTextureShader();
        static MaterialHandle getTextureMaterial();

        static bool registerFabricsAll();
        static void initializeMaterialDefaults(Material& material)
        {
            if (!material.containsVariable("tint"))
                material.setVector("tint", DirectX::XMFLOAT4(1, 1, 1, 1));
        }
    private:

    };
}

#endif