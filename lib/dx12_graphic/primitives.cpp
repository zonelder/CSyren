#include "pch.h"
#include "primitives.h"
#include "mesh.h"
#include "material.h"
#include "renderer.h"

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
        constexpr char LINE_MESH_NAME[]             = "__primitive_line__";
        constexpr char TRIANGLE_MESH_NAME[]         ="__primitive_triangle__";
        constexpr char QUAD_MESH_NAME[]             ="__primitive_quad__";
        constexpr char CUBE_MESH_NAME[]             ="__primitive_cube__";

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
               // pos = mul(pos, world);
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
            std::vector<Mesh::Vertex> verts = {
                { XMFLOAT3(0.0f, 0.0f, 0.0f), Color(1.0f, 1.0f, 1.0f, 1.0f) },
                { XMFLOAT3(1.0f, 0.0f, 0.0f), Color(1.0f, 1.0f, 1.0f, 1.0f) }
            };
            std::vector<uint16_t> idx = { 0, 1 };
            return rm.createMesh(LINE_MESH_NAME, verts, idx);
            };

        ResourceManager::ProceduralResourceFactory<Mesh> triangleMeshFabric = [](ResourceManager& rm) {
            std::vector<Mesh::Vertex> verts = {
                { XMFLOAT3(0.0f, 0.0f, 0.0f), Color(1.0f, 1.0f, 1.0f, 1.0f) },
                { XMFLOAT3(1.0f, 0.0f, 0.0f), Color(1.0f, 1.0f, 1.0f, 1.0f) },
                { XMFLOAT3(0.0f, 1.0f, 0.0f), Color(1.0f, 1.0f, 1.0f, 1.0f) }
            };
            std::vector<uint16_t> idx = { 0, 1, 2 };
            return rm.createMesh(TRIANGLE_MESH_NAME, verts, idx);
            };

        ResourceManager::ProceduralResourceFactory<Mesh> quadMeshFabric = [](ResourceManager& rm) {
            std::vector<Mesh::Vertex> verts = {
                { XMFLOAT3(-0.5f, -0.5f, 0.0f), Color(1.0f, 1.0f, 1.0f, 1.0f) },
                { XMFLOAT3(-0.5f,  0.5f, 0.0f), Color(1.0f, 1.0f, 1.0f, 1.0f) },
                { XMFLOAT3(0.5f,  0.5f, 0.0f), Color(1.0f, 1.0f, 1.0f, 1.0f) },
                { XMFLOAT3(0.5f, -0.5f, 0.0f), Color(1.0f, 1.0f, 1.0f, 1.0f) }
            };
            std::vector<uint16_t> idx = { 0, 1, 2, 0, 2, 3 };
            return rm.createMesh(QUAD_MESH_NAME, verts, idx);
            };

        ResourceManager::ProceduralResourceFactory<Mesh> cubeMeshFabric = [](ResourceManager& rm) {
            std::vector<Mesh::Vertex> verts = {
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
            return rm.createMesh(CUBE_MESH_NAME, verts, idx);
            };

        // --- Регистрация всех фабрик ---
        rm.registerProcedural(DEFAULT_SHADER_NAME, defaultShaderFabric);
        rm.registerProcedural(DEFAULT_MATERIAL_NAME, defaultMaterialFabric);
        rm.registerProcedural(LINE_MESH_NAME, lineMeshFabric);
        rm.registerProcedural(TRIANGLE_MESH_NAME, triangleMeshFabric);
        rm.registerProcedural(QUAD_MESH_NAME, quadMeshFabric);
        rm.registerProcedural(CUBE_MESH_NAME, cubeMeshFabric);

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

}