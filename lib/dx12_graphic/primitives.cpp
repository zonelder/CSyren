#include "pch.h"
#include "primitives.h"
#include "mesh.h"
#include "material.h"
#include "renderer.h"

#include <d3dcompiler.h>

using Microsoft::WRL::ComPtr;
using namespace DirectX;

namespace csyren::render
{
    namespace
    {
        //TODO create basic shaders file
        const char* g_vsCode =
            "struct VS_INPUT { float3 pos : POSITION; float4 color : COLOR; };"
            "struct PS_INPUT { float4 pos : SV_POSITION; float4 color : COLOR; };"
            "PS_INPUT main(VS_INPUT input) { PS_INPUT o; o.pos=float4(input.pos,1.0); o.color=input.color; return o; }";

        const char* g_psCode =
            "struct PS_INPUT { float4 pos : SV_POSITION; float4 color : COLOR; };"
            "float4 main(PS_INPUT input) : SV_TARGET { return input.color; }";
    }

    bool Primitives::createDefaultMaterial(Renderer& renderer, Material& material)
    {
        ComPtr<ID3DBlob> vs;
        ComPtr<ID3DBlob> ps;
        if (DX_FAILED(D3DCompile(g_vsCode, strlen(g_vsCode), nullptr, nullptr, nullptr,
            "main", "vs_5_0", 0, 0, &vs, nullptr)))
            return false;
        if (DX_FAILED(D3DCompile(g_psCode, strlen(g_psCode), nullptr, nullptr, nullptr,
            "main", "ps_5_0", 0, 0, &ps, nullptr)))
            return false;

        D3D12_SHADER_BYTECODE vbc{ vs->GetBufferPointer(), vs->GetBufferSize() };
        D3D12_SHADER_BYTECODE pbc{ ps->GetBufferPointer(), ps->GetBufferSize() };

        return material.init(renderer, vbc, pbc);
    }

    bool Primitives::createLine(Renderer& renderer, Mesh& mesh)
    {
        std::vector<Mesh::Vertex> verts =
        {
                { XMFLOAT3(0,0,0), XMFLOAT4(1,1,1,1) },
                { XMFLOAT3(1,0,0), XMFLOAT4(1,1,1,1) }
        };
        std::vector<uint16_t> idx = { 0,1 };
        return mesh.init(renderer, verts, idx);
    }

    bool Primitives::createTriangle(Renderer& renderer, Mesh& mesh)
    {
        std::vector<Mesh::Vertex> verts =
        {
                { XMFLOAT3(0,0,0), XMFLOAT4(1,1,1,1) },
                { XMFLOAT3(1,0,0), XMFLOAT4(1,1,1,1) },
                { XMFLOAT3(0,1,0), XMFLOAT4(1,1,1,1) }
        };
        std::vector<uint16_t> idx = { 0,1,2 };
        return mesh.init(renderer, verts, idx);
    }
}