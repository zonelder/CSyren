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
        constexpr char DEFAULT_MATERIAL_NAME[] =    "__primitive_default_material__";
        constexpr char LINE_MESH_NAME[] =           "__primitive_line__";
        constexpr char TRIANGLE_MESH_NAME[] =       "__primitive_triangle__";
        constexpr char QUAD_MESH_NAME[] =           "__primitive_quad__";
        constexpr char CUBE_MESH_NAME[] =           "__primitive_cube__";

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
            "   pos = mul(world,pos);"              // World transform
            "   pos = mul(viewProjection,pos);"     // Combined view-projection
            "   o.pos = pos;"
            "   o.color = input.color;"
            "   return o;"
            "}";

        const char* g_psCode =
            "struct PS_INPUT { float4 pos : SV_POSITION; float4 color : COLOR; };"
            "float4 main(PS_INPUT input) : SV_TARGET { return input.color; }";
    }

    MaterialHandle Primitives::getDefaultMaterial(ResourceManager& rm)
    {
        if (s_defaultMaterial && rm.getMaterial(s_defaultMaterial) != nullptr)
        {
            return s_defaultMaterial;
        }
        ComPtr<ID3DBlob> vs;
        ComPtr<ID3DBlob> ps;
        if (DX_FAILED(D3DCompile(g_vsCode, strlen(g_vsCode), nullptr, nullptr, nullptr,
            "main", "vs_5_0", 0, 0, &vs, nullptr)))
            return {};
        if (DX_FAILED(D3DCompile(g_psCode, strlen(g_psCode), nullptr, nullptr, nullptr,
            "main", "ps_5_0", 0, 0, &ps, nullptr)))
            return {};

        D3D12_SHADER_BYTECODE vbc{ vs->GetBufferPointer(), vs->GetBufferSize() };
        D3D12_SHADER_BYTECODE pbc{ ps->GetBufferPointer(), ps->GetBufferSize() };

        s_defaultMaterial = rm.loadMaterial(DEFAULT_MATERIAL_NAME, vbc, pbc);

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
                { XMFLOAT3(0,0,0), XMFLOAT4(1,1,1,1) },
                { XMFLOAT3(1,0,0), XMFLOAT4(1,1,1,1) }
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
                { XMFLOAT3(0,0,0), XMFLOAT4(1,1,1,1) },
                { XMFLOAT3(1,0,0), XMFLOAT4(1,1,1,1) },
                { XMFLOAT3(0,1,0), XMFLOAT4(1,1,1,1) }
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
            { XMFLOAT3(-0.5f, -0.5f, 0.0f), XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f) }, 
            { XMFLOAT3(-0.5f,  0.5f, 0.0f), XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f) },
            { XMFLOAT3(0.5f,  0.5f, 0.0f), XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f) },
            { XMFLOAT3(0.5f, -0.5f, 0.0f), XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f) }
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
            { XMFLOAT3(-0.5f, -0.5f, -0.5f), XMFLOAT4(1.0f, 0.0f, 0.0f, 1.0f) }, 
            { XMFLOAT3(-0.5f,  0.5f, -0.5f), XMFLOAT4(1.0f, 0.0f, 0.0f, 1.0f) }, 
            { XMFLOAT3(0.5f,  0.5f, -0.5f), XMFLOAT4(1.0f, 0.0f, 0.0f, 1.0f) }, 
            { XMFLOAT3(0.5f, -0.5f, -0.5f), XMFLOAT4(1.0f, 0.0f, 0.0f, 1.0f) },

            { XMFLOAT3(-0.5f, -0.5f,  0.5f), XMFLOAT4(0.0f, 1.0f, 0.0f, 1.0f) },
            { XMFLOAT3(-0.5f,  0.5f,  0.5f), XMFLOAT4(0.0f, 1.0f, 0.0f, 1.0f) },
            { XMFLOAT3(0.5f,  0.5f,  0.5f), XMFLOAT4(0.0f, 1.0f, 0.0f, 1.0f) }, 
            { XMFLOAT3(0.5f, -0.5f,  0.5f), XMFLOAT4(0.0f, 1.0f, 0.0f, 1.0f) } 
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