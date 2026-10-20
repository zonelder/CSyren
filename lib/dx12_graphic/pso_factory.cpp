#include "pch.h"
#include "pso_factory.h"
#include "compute_shader.h"

inline void hash_combine(std::size_t& seed, std::size_t hash)
{
    seed ^= hash + 0x9e3779b9 + (seed << 6) + (seed >> 2);
}

namespace csyren::render::details
{
    void PSOFactory::init()
    {
        _device =  core::Services::get<Renderer>()->device();
    }
    ID3D12PipelineState* PSOFactory::get(ShaderHandle sh,const GraphicShader* shader, const MaterialStateDesc& states,const VertexLayout& vertexLayout)
	{
        std::lock_guard lock(_mutex);
        PSOKey key{ sh, states,vertexLayout.getHash()};

        auto it = _psoCache.find(key);
        if (it != _psoCache.end())
            return it->second.Get();

        D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = {};
        psoDesc.pRootSignature = shader->getRootSignature();
        psoDesc.VS = shader->getVSBytecode();
        psoDesc.PS = shader->getPSBytecode();
        psoDesc.HS = shader->getHSBytecode();
        psoDesc.DS = shader->getDSBytecode();
        psoDesc.GS = shader->getGSBytecode();
        const auto& d3dLayout = vertexLayout.getD3DLayout();
        psoDesc.InputLayout = { d3dLayout.empty() ? nullptr : d3dLayout.data(), (UINT)d3dLayout.size() };

        psoDesc.BlendState = states.blendState;
        psoDesc.RasterizerState = states.rasterizerState;
        psoDesc.DepthStencilState = states.depthStencilState;
        psoDesc.PrimitiveTopologyType = states.primitiveTopologyType;
        psoDesc.NumRenderTargets = states.numRenderTargets;
        memcpy(psoDesc.RTVFormats, states.rtvFormats, sizeof(states.rtvFormats));
        psoDesc.DSVFormat = states.dsvFormat;
        psoDesc.SampleMask = UINT_MAX;
        psoDesc.SampleDesc.Count = 1;

        Microsoft::WRL::ComPtr<ID3D12PipelineState> pso;
        HRESULT hr = _device->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&pso));
        if (DX_FAILED(hr))
        {
            log::error("PSOFactory::get: Failed to create Graphics PSO (shader {} targets={} rtv0={} dsv={}).",
                sh.id, states.numRenderTargets, int(states.rtvFormats[0]), int(states.dsvFormat));
            return nullptr;
        }

        _psoCache[key] = pso;
        return pso.Get();
	}

    ID3D12PipelineState* PSOFactory::getCompute(ComputeShaderHandle sh, const ComputeShader* shader)
    {
        std::lock_guard lock(_mutex);
        auto it = _computeCache.find(sh);
        if (it != _computeCache.end())
            return it->second.Get();
        if (!shader) return nullptr;

        D3D12_COMPUTE_PIPELINE_STATE_DESC desc = {};
        desc.pRootSignature = shader->getRootSignature();
        desc.CS = shader->getCSBytecode();
        Microsoft::WRL::ComPtr<ID3D12PipelineState> pso;
        if (DX_FAILED(_device->CreateComputePipelineState(&desc, IID_PPV_ARGS(&pso))))
        {
            log::error("PSOFactory::getCompute: Failed to create Compute PSO (shader {}).", sh.id);
            return nullptr;
        }
        _computeCache[sh] = pso;
        return pso.Get();
    }


    std::size_t PSOKeyHasher ::operator()(const PSOKey& key) const
    {
        // 1. ShaderHandle
        std::size_t seed = std::hash<ShaderHandle>{}(key.shaderHandle);
        const size_t* data = reinterpret_cast<const size_t*>(&key.materialState);
        const size_t size = sizeof(MaterialStateDesc) / sizeof(size_t);

        static_assert(sizeof(MaterialStateDesc) % sizeof(size_t) == 0, "Invalid size for hashing");

        for (size_t i = 0; i < size; ++i) {
            hash_combine(seed, data[i]);
        }
        hash_combine(seed, key.vertexLayoutHash);

        return seed;
    }
}
