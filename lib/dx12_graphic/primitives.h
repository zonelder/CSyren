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

        // Deferred PBR materials (GBuffer): tint only, and tint * diffuseTexture.
        static ShaderHandle getLitShader();
        static ShaderHandle getLitTextureShader();
        static MaterialHandle getLitMaterial();
        // Forward PBR shader for transparent surfaces + an example glass material.
        static ShaderHandle getForwardLitShader();
        static MaterialHandle getGlassMaterial();
        // Alpha blending, depth test without depth write (transparent queue).
        static MaterialStateDesc transparentStates();

        static bool registerFabricsAll();
        // PBR parameters every lit shader declares; harmless for unlit shaders
        // (unknown variables are ignored when the cbuffer is filled).
        static void initializeMaterialDefaults(Material& material)
        {
            if (!material.containsVariable("tint"))
                material.setVector("tint", DirectX::XMFLOAT4(1, 1, 1, 1));
            if (!material.containsVariable("roughness"))
                material.setFloat("roughness", 0.75f);
            if (!material.containsVariable("metallic"))
                material.setFloat("metallic", 0.0f);
            if (!material.containsVariable("emissive"))
                material.setVector("emissive", DirectX::XMFLOAT4(0, 0, 0, 0));
        }
    private:

    };
}

#endif
