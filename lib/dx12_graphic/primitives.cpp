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
                float4 color    : COLOR;
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
                output.color = input.color;
                return output;
            }

            // --- Pixel Shader ---
            float4 PSMain(PS_Input input) : SV_TARGET
            {
                return input.color*tint;
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
                float4 color    : COLOR;
            };

            struct PS_Input
            {
                float4 position : SV_POSITION;
                float4 color    : COLOR;
            };

            PS_Input VSMain(VS_Input input)
            {
                PS_Input o;
                float4 pos = float4(input.position, 1.0f);
                pos = mul(pos, world);
                pos = mul(pos, viewProjection);
                o.position = pos;

                float wave = sin(time * 2.0f + input.position.x * 5.0f) * 0.5f + 0.5f;
                o.color = lerp(input.color, float4(wave, 1.0f - wave, 1.0f, 1.0f), 0.5f);
                return o;
            }

            float4 PSMain(PS_Input input) : SV_TARGET
            {
                float3 c = input.color.rgb * tint.rgb;
                return float4(c, 1.0f);
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

        ResourceManager::ProceduralResourceFactory<Mesh> lineMeshFabric = [](ResourceManager& rm) {
            std::vector<VertexXYZC> verts = {
                { XMFLOAT3(0.0f, 0.0f, 0.0f), Color(1.0f, 1.0f, 1.0f, 1.0f) },
                { XMFLOAT3(1.0f, 0.0f, 0.0f), Color(1.0f, 1.0f, 1.0f, 1.0f) }
            };
            std::vector<uint16_t> idx = { 0, 1 };
            return rm.createMesh(LINE_MESH_NAME, verts.data(),
                verts.size() * sizeof(VertexXYZC),
                sizeof(VertexXYZC),
                idx);
            };

        ResourceManager::ProceduralResourceFactory<Shader> rainbowShaderFabric =
            [](ResourceManager& rm) { return rm.createShaderFromCode(RAINBOW_SHADER_NAME, std::string(g_primitiveShaderCode2)); };

        ResourceManager::ProceduralResourceFactory<Material> rainbowMaterialFabric =
            [](ResourceManager& rm)
            {
                auto shader = rm.get<Shader>(RAINBOW_SHADER_NAME);
                MaterialStateDesc states = {};
                return rm.createMaterial(RAINBOW_MATERIAL_NAME, shader, states);
            };

        ResourceManager::ProceduralResourceFactory<Mesh> triangleMeshFabric = [](ResourceManager& rm) {
            std::vector<VertexXYZC> verts = {
                { XMFLOAT3(0.0f, 0.0f, 0.0f), Color(1.0f, 1.0f, 1.0f, 1.0f) },
                { XMFLOAT3(1.0f, 0.0f, 0.0f), Color(1.0f, 1.0f, 1.0f, 1.0f) },
                { XMFLOAT3(0.0f, 1.0f, 0.0f), Color(1.0f, 1.0f, 1.0f, 1.0f) }
            };
            std::vector<uint16_t> idx = { 0, 1, 2 };
            return rm.createMesh(TRIANGLE_MESH_NAME, verts.data(),verts.size()*sizeof(VertexXYZC),sizeof(VertexXYZC), idx);
            };

        ResourceManager::ProceduralResourceFactory<Mesh> quadMeshFabric = [](ResourceManager& rm) {
            std::vector<VertexXYZC> verts = {
                { XMFLOAT3(-0.5f, -0.5f, 0.0f), Color(1.0f, 1.0f, 1.0f, 1.0f) },
                { XMFLOAT3(-0.5f,  0.5f, 0.0f), Color(1.0f, 1.0f, 1.0f, 1.0f) },
                { XMFLOAT3(0.5f,  0.5f, 0.0f), Color(1.0f, 1.0f, 1.0f, 1.0f) },
                { XMFLOAT3(0.5f, -0.5f, 0.0f), Color(1.0f, 1.0f, 1.0f, 1.0f) }
            };
            std::vector<uint16_t> idx = { 0, 1, 2, 0, 2, 3 };
            return rm.createMesh(QUAD_MESH_NAME, verts.data(), verts.size() * sizeof(VertexXYZC), sizeof(VertexXYZC), idx);
            };

        ResourceManager::ProceduralResourceFactory<Mesh> cubeMeshFabric = [](ResourceManager& rm) {
            std::vector<VertexXYZC> verts = {
                { XMFLOAT3(-0.5f, -0.5f, -0.5f), Color(1.0f, 0.0f, 0.0f, 1.0f) },
                { XMFLOAT3(-0.5f,  0.5f, -0.5f), Color(1.0f, 0.0f, 0.0f, 1.0f) },
                { XMFLOAT3(0.5f,  0.5f, -0.5f), Color(1.0f, 0.0f, 0.0f, 1.0f) },
                { XMFLOAT3(0.5f, -0.5f, -0.5f), Color(1.0f, 0.0f, 0.0f, 1.0f) },
                { XMFLOAT3(-0.5f, -0.5f,  0.5f), Color(0.0f, 1.0f, 0.0f, 1.0f) },
                { XMFLOAT3(-0.5f,  0.5f,  0.5f), Color(0.0f, 1.0f, 0.0f, 1.0f) },
                { XMFLOAT3(0.5f,  0.5f,  0.5f), Color(0.0f, 1.0f, 0.0f, 1.0f) },
                { XMFLOAT3(0.5f, -0.5f,  0.5f), Color(0.0f, 1.0f, 0.0f, 1.0f) }
            };
            std::vector<uint16_t> idx = {
                0, 1, 2,   0, 2, 3,
                7, 6, 5,   7, 5, 4,
                4, 5, 1,   4, 1, 0,
                3, 2, 6,   3, 6, 7,
                1, 5, 6,   1, 6, 2,
                4, 0, 3,   4, 3, 7
            };
            return rm.createMesh(CUBE_MESH_NAME, verts.data(), verts.size() * sizeof(VertexXYZC), sizeof(VertexXYZC), idx);
            };

        ResourceManager::ProceduralResourceFactory<Mesh> sphereMeshFabric = [](ResourceManager& rm) {
                const int latitudeBands = 16;
                const int longitudeBands = 16;
                const float radius = 0.5f;

                std::vector<VertexXYZC> verts;
                std::vector<uint16_t> idx;

                verts.reserve((latitudeBands + 1) * (longitudeBands + 1));
                idx.reserve(latitudeBands * longitudeBands * 6);

                for (int lat = 0; lat <= latitudeBands; ++lat)
                {
                    float theta = lat * DirectX::XM_PI / latitudeBands;
                    float sinTheta = sinf(theta);
                    float cosTheta = cosf(theta);

                    for (int lon = 0; lon <= longitudeBands; ++lon)
                    {
                        float phi = lon * 2.0f * DirectX::XM_PI / longitudeBands;
                        float sinPhi = sinf(phi);
                        float cosPhi = cosf(phi);

                        float x = cosPhi * sinTheta;
                        float y = cosTheta;
                        float z = sinPhi * sinTheta;

                        VertexXYZC v;
                        v.pos = DirectX::XMFLOAT3(x * radius, y * radius, z * radius);
                        v.color = Color(
                            0.5f + 0.5f * x,
                            0.5f + 0.5f * y,
                            0.5f + 0.5f * z,
                            1.0f
                        );
                        verts.push_back(v);
                    }
                }

                for (int lat = 0; lat < latitudeBands; ++lat)
                {
                    for (int lon = 0; lon < longitudeBands; ++lon)
                    {
                        int first = (lat * (longitudeBands + 1)) + lon;
                        int second = first + longitudeBands + 1;

                        idx.push_back(static_cast<uint16_t>(first));
                        idx.push_back(static_cast<uint16_t>(second));
                        idx.push_back(static_cast<uint16_t>(first + 1));

                        idx.push_back(static_cast<uint16_t>(second));
                        idx.push_back(static_cast<uint16_t>(second + 1));
                        idx.push_back(static_cast<uint16_t>(first + 1));
                    }
                }
                return rm.createMesh(SPHERE_MESH_NAME, verts.data(), verts.size() * sizeof(VertexXYZC), sizeof(VertexXYZC), idx);
            };

        // --- Регистрация всех фабрик ---
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