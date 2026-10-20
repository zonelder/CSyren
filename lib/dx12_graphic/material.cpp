#include "pch.h"
#include "material.h"
#include "resource_manager.h"
#include "primitives.h"
#include "import/model_importer.h"

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
        rasterizerState.CullMode = D3D12_CULL_MODE_NONE;
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
        depthStencilState.DepthEnable = TRUE;
        depthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
        depthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_GREATER_EQUAL;
        depthStencilState.StencilEnable = FALSE;
        depthStencilState.StencilReadMask = D3D12_DEFAULT_STENCIL_READ_MASK;
        depthStencilState.StencilWriteMask = D3D12_DEFAULT_STENCIL_WRITE_MASK; 

        // --- Остальные параметры PSO ---
        primitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
        numRenderTargets = 1;
        rtvFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
        for (int i = 1; i < D3D12_SIMULTANEOUS_RENDER_TARGET_COUNT; ++i) {
            rtvFormats[i] = DXGI_FORMAT_UNKNOWN;
        }
        dsvFormat = DXGI_FORMAT_D32_FLOAT;
    }

    bool Material::init(const std::string& filepath)
    {
        // "<model asset>#mat:<index|name>": a lit material with the model's base
        // color factor and (when present) its base color texture.
        const auto reference = assets::ModelReference::parse(filepath);
        if (!reference.valid || reference.kind != assets::ModelReference::Kind::Material)
        {
            log::error("Material: '{}' is not a model material reference (expected <model>#mat:<name|index>)", filepath);
            return false;
        }
        const auto model = assets::ModelImporterRegistry::instance().load(reference.asset);
        if (!model) return false;
        const auto* source = assets::resolveMaterial(*model, reference);
        if (!source) return false;
        auto& resources = *core::Services::get<ResourceManager>();
        const bool textured = !source->baseColorTexture.empty();
        const ShaderHandle shader = textured ? Primitives::getLitTextureShader() : Primitives::getLitShader();
        MaterialStateDesc states = {};
        if (!init(shader, states)) return false;
        Primitives::initializeMaterialDefaults(*this);
        setVector("tint", DirectX::XMFLOAT4(source->baseColor.x, source->baseColor.y, source->baseColor.z, source->baseColor.w));
        // glTF defaults to a fully metallic rough surface; untextured placeholder
        // models look better as dielectrics, so only honor explicit values.
        setFloat("roughness", source->roughness);
        setFloat("metallic", source->metallic >= 0.999f && source->roughness >= 0.999f ? 0.0f : source->metallic);
        setVector("emissive", DirectX::XMFLOAT4(source->emissive.x, source->emissive.y, source->emissive.z, 1.0f));
        if (textured) setTexture("diffuseTexture", resources.get<Texture>(source->baseColorTexture));
        return true;
    }

    bool Material::init(ShaderHandle shaderHandle, const MaterialStateDesc& states)
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
