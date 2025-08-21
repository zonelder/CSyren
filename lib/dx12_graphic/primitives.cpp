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
        constexpr char HARDCODE_SHADER_NAME[]       = "__csyren_hardcode_shader__";
        constexpr char DEFAULT_SHADER_NAME[]        = "__csyren_default_shader__";
        constexpr char DEFAULT_MATERIAL_NAME[]      = "__primitive_default_material__";
        constexpr char LINE_MESH_NAME[]             = "__primitive_line__";
        constexpr char TRIANGLE_MESH_NAME[]         ="__primitive_triangle__";
        constexpr char QUAD_MESH_NAME[]             ="__primitive_quad__";
        constexpr char CUBE_MESH_NAME[]             ="__primitive_cube__";

        const char* g_hardcodedVS = R"(
            struct PS_INPUT {
                float4 pos : SV_POSITION;
            };

            PS_INPUT main(uint vertexId : SV_VertexID) {
                PS_INPUT o;
                // Жестко задаем координаты вершин в пространстве отсечения (NDC)
                // Это стандартный тестовый треугольник, который должен занять часть экрана
                if (vertexId == 0) o.pos = float4(-0.5, -0.5, 0.5, 1.0);
                if (vertexId == 1) o.pos = float4( 0.0,  0.5, 0.5, 1.0);
                if (vertexId == 2) o.pos = float4( 0.5, -0.5, 0.5, 1.0);
                return o;
            }
        )";

        const char* g_hardcodedPS = R"(
            float4 main() : SV_TARGET {
                // Просто возвращаем белый цвет
                return float4(1.0, 1.0, 1.0, 1.0);
            }
        )";

        //TODO create basic shaders file
        const char* g_vsCode =
            "cbuffer PerFrame : register(b0) {"
            "   matrix projection;"
            "   matrix view;"
            "   matrix viewProjection;"
            "   matrix invView;"
            "}"
            "cbuffer PerObject : register(b1) { matrix world; }"
            "struct VS_INPUT { float3 pos : POSITION; float4 color : COLOR; };"
            "struct PS_INPUT { float4 pos : SV_POSITION; float4 color : COLOR; };"
            "PS_INPUT main(VS_INPUT input) {"
            "   PS_INPUT o;"
            "   float4 pos = float4(input.pos, 1.0);"
            //"   pos = mul(world,pos);"              // World transform
            //"   pos = mul(viewProjection,pos);"     // Combined view-projection
            "   o.pos = pos;"
            "   o.color = input.color;"
            "   return o;"
            "}";

        const char* g_psCode =
            "struct PS_INPUT { float4 pos : SV_POSITION; float4 color : COLOR; };"
            "float4 main(PS_INPUT input) : SV_TARGET { return input.color; }";
    }

    MaterialHandle Primitives::getHardcodedMaterial(ResourceManager& rm)
    {
        if (s_hardcodedMaterial && rm.getMaterial(s_hardcodedMaterial) != nullptr)
        {
            return s_hardcodedMaterial;
        }
        auto shader = rm.loadShader("__HARDCODE_SHADER__", std::string(g_hardcodedVS), std::string(g_hardcodedPS), L"hardcode");
        MaterialStateDesc defaultStates = {};
        s_hardcodedMaterial = rm.loadMaterial(HARDCODE_SHADER_NAME, shader, defaultStates);
        return s_hardcodedMaterial;
    }

    ShaderHandle Primitives::getDefaultShader(ResourceManager& rm)
    {
        if (s_defaultShader && rm.getShader(s_defaultShader) != nullptr)
        {
            return s_defaultShader;
        }

        s_defaultShader = rm.loadShader(DEFAULT_SHADER_NAME, std::string(g_vsCode), std::string(g_psCode),L"default");
        return s_defaultShader;
    }

    MaterialHandle Primitives::getDefaultMaterial(ResourceManager& rm)
    {
        if (s_defaultMaterial && rm.getMaterial(s_defaultMaterial) != nullptr)
        {
            return s_defaultMaterial;
        }
        // 1. Получаем хендл на дефолтный шейдер
        ShaderHandle shaderHandle = getDefaultShader(rm);
        if (!shaderHandle)
        {
            log::error("Primitives: Failed to get default shader.");
            return {};
        }
        MaterialStateDesc defaultStates = {};
        s_defaultMaterial = rm.loadMaterial(DEFAULT_MATERIAL_NAME, shaderHandle, defaultStates);

        return s_defaultMaterial;
    }

    MeshHandle Primitives::getLine(ResourceManager& rm)
    {


        if (s_lineMesh && rm.getMesh(s_lineMesh) != nullptr)
        {
            return s_lineMesh;
        }

        std::vector<Mesh::Vertex> verts =
        {
                { XMFLOAT3(0,0,0), Color(1,1,1,1) },
                { XMFLOAT3(1,0,0), Color(1,1,1,1) }
        };
        std::vector<uint16_t> idx = { 0,1 };
        s_lineMesh = rm.loadMesh(LINE_MESH_NAME, verts, idx);
        return s_lineMesh;
    }

    MeshHandle Primitives::getTriangle(ResourceManager& rm)
    {
        if (s_triangleMesh && rm.getMesh(s_triangleMesh) != nullptr)
        {
            return s_triangleMesh;
        }

        std::vector<Mesh::Vertex> verts =
        {
                { XMFLOAT3(0,0,0), Color(1,1,1,1) },
                { XMFLOAT3(1,0,0), Color(1,1,1,1) },
                { XMFLOAT3(0,1,0), Color(1,1,1,1) }
        };
        std::vector<uint16_t> idx = { 0,1,2 };
        
        s_triangleMesh = rm.loadMesh(TRIANGLE_MESH_NAME, verts, idx);
        return s_triangleMesh;
    }



    MeshHandle Primitives::getQuad(ResourceManager& rm)
    {
        if (s_quadMesh && rm.getMesh(s_quadMesh) != nullptr)
        {
            return s_quadMesh;
        }

        std::vector<Mesh::Vertex> verts =
        {
            { XMFLOAT3(-0.5f, -0.5f, 0.0f), Color(1.0f, 1.0f, 1.0f, 1.0f) },
            { XMFLOAT3(-0.5f,  0.5f, 0.0f), Color(1.0f, 1.0f, 1.0f, 1.0f) },
            { XMFLOAT3(0.5f,  0.5f, 0.0f), Color(1.0f, 1.0f, 1.0f, 1.0f) },
            { XMFLOAT3(0.5f, -0.5f, 0.0f), Color(1.0f, 1.0f, 1.0f, 1.0f) }
        };

        std::vector<uint16_t> idx =
        {
            0, 1, 2, 
            0, 2, 3 
        };

        s_quadMesh = rm.loadMesh(QUAD_MESH_NAME, verts, idx);
        return s_quadMesh;
    }

    MeshHandle Primitives::getCube(ResourceManager& rm)
    {
        if (s_cubeMesh && rm.getMesh(s_cubeMesh) != nullptr)
        {
            return s_cubeMesh;
        }

        std::vector<Mesh::Vertex> verts =
        {
            { XMFLOAT3(-0.5f, -0.5f, -0.5f), Color(1.0f, 0.0f, 0.0f, 1.0f) },
            { XMFLOAT3(-0.5f,  0.5f, -0.5f), Color(1.0f, 0.0f, 0.0f, 1.0f) },
            { XMFLOAT3(0.5f,  0.5f, -0.5f), Color(1.0f, 0.0f, 0.0f, 1.0f) },
            { XMFLOAT3(0.5f, -0.5f, -0.5f), Color(1.0f, 0.0f, 0.0f, 1.0f) },

            { XMFLOAT3(-0.5f, -0.5f,  0.5f), Color(0.0f, 1.0f, 0.0f, 1.0f) },
            { XMFLOAT3(-0.5f,  0.5f,  0.5f), Color(0.0f, 1.0f, 0.0f, 1.0f) },
            { XMFLOAT3(0.5f,  0.5f,  0.5f), Color(0.0f, 1.0f, 0.0f, 1.0f) },
            { XMFLOAT3(0.5f, -0.5f,  0.5f), Color(0.0f, 1.0f, 0.0f, 1.0f) }
        };

        std::vector<uint16_t> idx =
        {
            0, 1, 2,   0, 2, 3,
            7, 6, 5,   7, 5, 4,
            4, 5, 1,   4, 1, 0,
            3, 2, 6,   3, 6, 7,
            1, 5, 6,   1, 6, 2,
            4, 0, 3,   4, 3, 7
        };

        // 3. Обновляем кеш и возвращаем результат
        s_cubeMesh = rm.loadMesh(CUBE_MESH_NAME, verts, idx);
        return s_cubeMesh;
    }

    /// @brief 
    void Primitives::clearCache()
    {
        s_defaultMaterial = {};
        s_lineMesh = {};
        s_triangleMesh = {};
        s_quadMesh = {};
        s_cubeMesh = {};
    }
}