#include "pch.h"
#include "material.h"
#include "resource_manager.h"

#include <d3dcompiler.h>

using Microsoft::WRL::ComPtr;

namespace csyren::render
{



    MaterialStateDesc::MaterialStateDesc()
    {
        // --- Blend State ---
        blendState = {};
        blendState.AlphaToCoverageEnable = FALSE;
        blendState.IndependentBlendEnable = FALSE;
        blendState.RenderTarget[0].BlendEnable = FALSE;
        blendState.RenderTarget[0].LogicOpEnable = FALSE;
        blendState.RenderTarget[0].SrcBlend = D3D12_BLEND_ONE;
        blendState.RenderTarget[0].DestBlend = D3D12_BLEND_ZERO;
        blendState.RenderTarget[0].BlendOp = D3D12_BLEND_OP_ADD;
        blendState.RenderTarget[0].SrcBlendAlpha = D3D12_BLEND_ONE;
        blendState.RenderTarget[0].DestBlendAlpha = D3D12_BLEND_ZERO;
        blendState.RenderTarget[0].BlendOpAlpha = D3D12_BLEND_OP_ADD;
        blendState.RenderTarget[0].LogicOp = D3D12_LOGIC_OP_NOOP;
        blendState.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;

        // --- Rasterizer State ---
        rasterizerState = {};
        rasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
        rasterizerState.CullMode = D3D12_CULL_MODE_NONE; // ¬ы можете изменить это на D3D12_CULL_MODE_NONE дл€ отладки
        rasterizerState.FrontCounterClockwise = FALSE;
        rasterizerState.DepthBias = D3D12_DEFAULT_DEPTH_BIAS;
        rasterizerState.DepthBiasClamp = D3D12_DEFAULT_DEPTH_BIAS_CLAMP;
        rasterizerState.SlopeScaledDepthBias = D3D12_DEFAULT_SLOPE_SCALED_DEPTH_BIAS;
        rasterizerState.DepthClipEnable = TRUE;
        rasterizerState.MultisampleEnable = FALSE;
        rasterizerState.AntialiasedLineEnable = FALSE;
        rasterizerState.ForcedSampleCount = 0;
        rasterizerState.ConservativeRaster = D3D12_CONSERVATIVE_RASTERIZATION_MODE_OFF;

        // --- Depth-Stencil State ---
        depthStencilState = {};
        depthStencilState.DepthEnable = TRUE; // ќбычно это то, что вам нужно
        depthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
        depthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_LESS;
        depthStencilState.StencilEnable = FALSE;
        depthStencilState.StencilReadMask = D3D12_DEFAULT_STENCIL_READ_MASK;
        depthStencilState.StencilWriteMask = D3D12_DEFAULT_STENCIL_WRITE_MASK;
        // ... остальна€ часть depthStencilState игнорируетс€ ...

        // --- ќстальные параметры PSO ---
        primitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
        numRenderTargets = 1;
        rtvFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
        for (int i = 1; i < D3D12_SIMULTANEOUS_RENDER_TARGET_COUNT; ++i) {
            rtvFormats[i] = DXGI_FORMAT_UNKNOWN;
        }
        dsvFormat = DXGI_FORMAT_D32_FLOAT;
    }

    bool Material::init(Renderer& renderer, const std::string& filepath)
    {
        log::error("Material: material loading from file not implemented. file = {}", filepath);
        return false;
    }

    bool Material::init(Renderer& renderer, ShaderHandle shaderHandle, const MaterialStateDesc& states)
    {
        if (!shaderHandle) {
            log::error("Material::init: Invalid shader handle provided.");
            return false;
        }
        _states = states;
        _shaderHandle = shaderHandle;
        return true;
    }
}
