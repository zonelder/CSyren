#include "pch.h"
#include "primitives.h"
#include "mesh.h"
#include "material.h"
#include "renderer.h"
#include "vertex_formats.h"

#include <d3dcompiler.h>
#include <vector>

using Microsoft::WRL::ComPtr;
using namespace DirectX;

namespace csyren::render
{
    namespace
    {
        constexpr char TEXTURE_SHADER_NAME[]        = "__csyren_texture_shader__";
        constexpr char TEXTURE_MATERIAL_NAME[]      = "__primitive_texture_material__";

        constexpr char DEFAULT_SHADER_NAME[]        = "__csyren_default_shader__";
        constexpr char DEFAULT_MATERIAL_NAME[]      = "__primitive_default_material__";

        constexpr char RAINBOW_SHADER_NAME[]        = "__csyren_rainbow_shader__";
        constexpr char RAINBOW_MATERIAL_NAME[]      = "__primitive_rainbow_material__";
        constexpr char LINE_MESH_NAME[]             = "__primitive_line__";
        constexpr char TRIANGLE_MESH_NAME[]         ="__primitive_triangle__";
        constexpr char QUAD_MESH_NAME[]             ="__primitive_quad__";
        constexpr char CUBE_MESH_NAME[]             ="__primitive_cube__";
        constexpr char SPHERE_MESH_NAME[]           = "__primitive_sphere__";
        

        //TODO create basic shaders file
        const char* g_primitiveShaderCode = R"(
            
            //@update frame
            cbuffer perFrame
            {
                //@semantic ViewProjection
               matrix viewProjection;
            }

            //@update entity
            cbuffer perEntity
            {
                //@semantic World
                matrix world;
            }

            //@update material
            cbuffer material
            {
                //@editable 
                float4 tint;
            }

            struct VS_Input
            {
                float3 position : POSITION;
            };

            struct PS_Input
            {
                float4 position : SV_POSITION;
                float4 color    : COLOR;
            };

            // --- Vertex Shader ---
            PS_Input VSMain(VS_Input input)
            {
                PS_Input output;
                float4 pos = float4(input.position, 1.0f);
                pos = mul(pos, world);
                pos = mul(pos, viewProjection);
        
                output.position = pos;
                return output;
            }

            // --- Pixel Shader ---
            float4 PSMain(PS_Input input) : SV_TARGET
            {
                return tint;
            }
        )";

            const char* g_primitiveShaderCode2 = R"(

            //@update frame
            cbuffer perFrame
            {
                //@semantic ViewProjection
                matrix viewProjection;
                //@semantic Time
                float time;
                //@semantic LightDirection
                float4 lightDir;
            }

            //@update entity
            cbuffer perEntity
            {
                //@semantic World
                matrix world;
            }

            //@update material
            cbuffer material
            {
                //@editable
                float4 tint;
            }

            struct VS_Input
            {
                float3 position : POSITION;
                float3 normal   : NORMAL;
            };

            struct PS_Input
            {
                float4 position : SV_POSITION;
                float3 normal   : NORMAL;
            };

            PS_Input VSMain(VS_Input input)
            {
                PS_Input o;
                float4 pos = float4(input.position, 1.0f);
                pos = mul(pos, world);
                pos = mul(pos, viewProjection);
                o.position = pos;
                o.normal = mul(input.normal,(float3x3)world);
                o.normal = normalize(o.normal);
                return o;
            }

            float4 PSMain(PS_Input input) : SV_TARGET
            {
                float3 L = normalize(-lightDir);
                float3 N = normalize(input.normal);

                // diffuse
                float NdotL = max(dot(N, L), 0.0f);

                // базовый цвет
                float3 c = tint.rgb * NdotL;

                return float4(c, 1.0f);
            }
        )";

        const char* g_primitiveShaderCode3 = R"(
            
            //@update frame
            cbuffer perFrame
            {
                //@semantic ViewProjection
               matrix viewProjection;
            }

            //@update entity
            cbuffer perEntity
            {
                //@semantic World
                matrix world;
            }

            struct VS_Input
            {
                float3 position : POSITION;
                float2 uv : TEXCOORD0;
            };

            struct PS_Input
            {
                float4 position : SV_POSITION;
                float2 uv       : TEXCOORD0;
            };
            
            Texture2D diffuseTexture : register(t0);
            SamplerState samplerLinear : register(s0);


            // --- Vertex Shader ---
            PS_Input VSMain(VS_Input input)
            {
                PS_Input output;
                float4 pos = float4(input.position, 1.0f);
                pos = mul(pos, world);
                pos = mul(pos, viewProjection);
                output.position = pos;

                output.uv = input.uv;
                return output;
            }

            // --- Pixel Shader ---
            float4 PSMain(PS_Input input) : SV_TARGET
            {
                float4 color = diffuseTexture.Sample(samplerLinear, input.uv);
                return color;
            }
        )";
    }


    bool Primitives::registerFabricsAll(ResourceManager& rm)
    {
        ResourceManager::ProceduralResourceFactory<Shader> defaultShaderFabric = [](ResourceManager& rm) { return rm.createShaderFromCode(DEFAULT_SHADER_NAME, std::string(g_primitiveShaderCode)); };

        ResourceManager::ProceduralResourceFactory<Material> defaultMaterialFabric = [](ResourceManager& rm)
            {
                auto shader = rm.get<Shader>(DEFAULT_SHADER_NAME);
                MaterialStateDesc defaultStates = {};
                return rm.createMaterial(DEFAULT_MATERIAL_NAME, shader, defaultStates);
            };
        ResourceManager::ProceduralResourceFactory<Material> textureMaterialFabric = [](ResourceManager& rm)
            {
                auto shader = rm.get<Shader>(TEXTURE_SHADER_NAME);
                MaterialStateDesc defaultStates = {};
                return rm.createMaterial(TEXTURE_MATERIAL_NAME, shader, defaultStates);
            };

        ResourceManager::ProceduralResourceFactory<Mesh> lineMeshFabric = [](ResourceManager& rm) {
            MeshBuilder builder;
            builder
                .addVertex({ 0,0,0 }).addColor({ 1,1,1,1 })
                .addVertex({ 1,0,0 }).addColor({ 1,1,1,1 })
                .addIndex(0).addIndex(1);
            return rm.createMesh(LINE_MESH_NAME,builder);
            };

        ResourceManager::ProceduralResourceFactory<Shader> rainbowShaderFabric =
            [](ResourceManager& rm) { return rm.createShaderFromCode(RAINBOW_SHADER_NAME, std::string(g_primitiveShaderCode2)); };


        ResourceManager::ProceduralResourceFactory<Shader> textureShaderFabric =
            [](ResourceManager& rm) { return rm.createShaderFromCode(TEXTURE_SHADER_NAME, std::string(g_primitiveShaderCode3)); };


        ResourceManager::ProceduralResourceFactory<Material> rainbowMaterialFabric =
            [](ResourceManager& rm)
            {
                auto shader = rm.get<Shader>(RAINBOW_SHADER_NAME);
                MaterialStateDesc states = {};
                return rm.createMaterial(RAINBOW_MATERIAL_NAME, shader, states);
            };

        ResourceManager::ProceduralResourceFactory<Mesh> triangleMeshFabric = [](ResourceManager& rm) {
            MeshBuilder builder;
            builder
                .addVertex({ 0,0,0 }).addColor({ 1, 1, 1, 1 })
                .addVertex({ 1,0,0 }).addColor({ 1, 1, 1, 1 })
                .addVertex({ 0,1,0 }).addColor({ 1, 1, 1, 1 })
                .addTriangle(0, 1, 2);
            return rm.createMesh(TRIANGLE_MESH_NAME,builder);
            };

        ResourceManager::ProceduralResourceFactory<Mesh> quadMeshFabric = [](ResourceManager& rm) {
            MeshBuilder builder;
            std::array<math::Vector3, 4> positions = 
            {
                math::Vector3{-0.5f, 0.0f, -0.5f},
                math::Vector3{-0.5f, 0.0f,  0.5f},
                math::Vector3{ 0.5f, 0.0f,  0.5f},
                math::Vector3{ 0.5f, 0.0f, -0.5f}
            };

            math::Vector3 normal{ 0.0f, 1.0f, 0.0f };

            vertex_meta::color_type color(1.0f, 1.0f, 1.0f, 1.0f);

            std::array<math::Vector2, 4> uvs = {
                math::Vector2{0.0f, 1.0f},
                math::Vector2{0.0f, 0.0f},
                math::Vector2{1.0f, 0.0f},
                math::Vector2{1.0f, 1.0f}
            };

            for (size_t i = 0; i < 4; ++i) {
                builder.addVertex(positions[i])
                    .addNormal(normal)
                    .addUV(uvs[i])
                    .addColor(color);
            }

            builder.addTriangle(0, 1, 2)
                .addTriangle(0, 2, 3);

            return rm.createMesh(QUAD_MESH_NAME, builder);
            };

        ResourceManager::ProceduralResourceFactory<Mesh> cubeMeshFabric = [](ResourceManager& rm) {
            MeshBuilder builder;
            const float hs = 0.5f; // half-size
            struct Face
            {
                math::Vector3 normal;
                vertex_meta::color_type color;
                std::array<math::Vector3, 4> positions;
            };
            std::array<Face, 6> faces =
            {
                Face{{0,0,1},{1,1,1,1},  {{ { -hs,-hs, hs }, {hs,-hs, hs}, {hs,hs, hs}, {-hs,hs, hs} } }},    // front
                Face{{0,0,-1},{1,1,1,1}, {{ {hs,-hs,-hs}, {-hs,-hs,-hs}, {-hs,hs,-hs}, {hs,hs,-hs}   }},},   // back
                Face{{-1,0,0},{1,1,1,1}, {{ {-hs,-hs,-hs}, {-hs,-hs, hs}, {-hs,hs, hs}, {-hs,hs,-hs} }},},   // left
                Face{{1,0,0},{1,1,1,1},  {{ {hs,-hs, hs}, {hs,-hs,-hs}, {hs,hs,-hs}, {hs,hs, hs}     }},},   // right
                Face{{0,1,0},{1,1,1,1},  {{ {-hs, hs, hs}, {hs, hs, hs}, {hs, hs,-hs}, {-hs, hs,-hs} }},},   // top
                Face{{0,-1,0},{1,1,1,1}, {{ {-hs,-hs,-hs}, {hs,-hs,-hs}, {hs,-hs, hs}, {-hs,-hs, hs} }},}    // bottom
            };
            uint32_t vertexOffset = 0;

            for (const auto& f : faces)
            {
                for (size_t i = 0; i < 4; ++i)
                {
                    builder.addVertex(f.positions[i]);
                    builder.addNormal(f.normal);
                    builder.addColor(f.color);
                }
                builder.addTriangle(vertexOffset + 0, vertexOffset + 1, vertexOffset + 2);
                builder.addTriangle(vertexOffset + 0, vertexOffset + 2, vertexOffset + 3);

                vertexOffset += 4;
            }
            return rm.createMesh(CUBE_MESH_NAME, builder);
            };

        ResourceManager::ProceduralResourceFactory<Mesh> sphereMeshFabric = [](ResourceManager& rm) {
            const int latBands = 16;
            const int longBands = 16;
            const float radius = 0.5f;
            MeshBuilder builder;

            for (int lat = 0; lat <= latBands; lat++)
            {
                float theta = lat * XM_PI / latBands;
                float sinTheta = sinf(theta);
                float cosTheta = cosf(theta);
                for (int lon = 0; lon <= longBands; lon++)
                {
                    float phi = lon * 2.0f * XM_PI / longBands;
                    float sinPhi = sinf(phi);
                    float cosPhi = cosf(phi);

                    math::Vector3 pos{ cosPhi * sinTheta * radius, cosTheta * radius, sinPhi * sinTheta * radius };
                    render::vertex_meta::color_type c{ 0.5f + 0.5f * pos.x, 0.5f + 0.5f * pos.y, 0.5f + 0.5f * pos.z,1.0f };
                    math::Vector3 normal = pos; // center in (0,0,0)
                    normal.normalize();
                    math::Vector2 uv{ static_cast<float>(lon) / longBands, static_cast<float>(lat) / latBands };

                    builder.addVertex(pos).addColor(c).addNormal(normal).addUV(uv);
                }
            }

            for (int lat = 0; lat < latBands; lat++)
            {
                for (int lon = 0; lon < longBands; lon++)
                {
                    int first = lat * (longBands + 1) + lon;
                    int second = first + longBands + 1;

                    builder.addTriangle(first, second, first + 1);
                    builder.addTriangle(second, second + 1, first + 1);
                }
            }

                return rm.createMesh(SPHERE_MESH_NAME, builder);
            };

        // --- Регистрация всех фабрик ---
        rm.registerProcedural(TEXTURE_SHADER_NAME, textureShaderFabric);
        rm.registerProcedural(TEXTURE_MATERIAL_NAME, textureMaterialFabric);

        rm.registerProcedural(DEFAULT_SHADER_NAME, defaultShaderFabric);
        rm.registerProcedural(DEFAULT_MATERIAL_NAME, defaultMaterialFabric);
        rm.registerProcedural(LINE_MESH_NAME, lineMeshFabric);
        rm.registerProcedural(TRIANGLE_MESH_NAME, triangleMeshFabric);
        rm.registerProcedural(QUAD_MESH_NAME, quadMeshFabric);
        rm.registerProcedural(CUBE_MESH_NAME, cubeMeshFabric);
        rm.registerProcedural(RAINBOW_SHADER_NAME, rainbowShaderFabric);
        rm.registerProcedural(RAINBOW_MATERIAL_NAME, rainbowMaterialFabric);
        rm.registerProcedural(SPHERE_MESH_NAME,sphereMeshFabric);

        return true; // Возвращаем true в случае успеха
    }
    ShaderHandle Primitives::getTextureShader(ResourceManager& rm)
    {
        return rm.get<Shader>(TEXTURE_SHADER_NAME);
    }
    MaterialHandle Primitives::getTextureMaterial(ResourceManager& rm)
    {
        return rm.get<Material>(TEXTURE_MATERIAL_NAME);
    }

    ShaderHandle Primitives::getDefaultShader(ResourceManager& rm)
    {
        return rm.get<Shader>(DEFAULT_SHADER_NAME);
    }

    MaterialHandle Primitives::getDefaultMaterial(ResourceManager& rm)
    {
        return rm.get<Material>(DEFAULT_MATERIAL_NAME);
    }

    ShaderHandle Primitives::getRainbowShader(ResourceManager& rm)
    {
        return rm.get<Shader>(RAINBOW_SHADER_NAME);
    }

    MaterialHandle Primitives::getRainbowMaterial(ResourceManager& rm)
    {
        return rm.get<Material>(RAINBOW_MATERIAL_NAME);
    }

    MeshHandle Primitives::getLine(ResourceManager& rm)
    {
        return rm.get<Mesh>(LINE_MESH_NAME);
    }

    MeshHandle Primitives::getTriangle(ResourceManager& rm)
    {
        return rm.get<Mesh>(TRIANGLE_MESH_NAME);
    }



    MeshHandle Primitives::getQuad(ResourceManager& rm)
    {
        return rm.get<Mesh>(QUAD_MESH_NAME);
    }

    MeshHandle Primitives::getCube(ResourceManager& rm)
    {
        return rm.get<Mesh>(CUBE_MESH_NAME);
    }

    MeshHandle Primitives::getSphere(ResourceManager& rm)
    {
        return rm.get<Mesh>(SPHERE_MESH_NAME);
    }


}