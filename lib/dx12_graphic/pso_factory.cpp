#include "pch.h"
#include "pso_factory.h"

inline void hash_combine(std::size_t& seed, std::size_t hash) 
{
    seed ^= hash + 0x9e3779b9 + (seed << 6) + (seed >> 2);
}

namespace csyren::render::details
{
	ID3D12PipelineState* PSOFactory::get(ShaderHandle sh,Shader* shader, const MaterialStateDesc& states,const VertexLayout& vertexLayout)
	{
        PSOKey key{ sh, states,vertexLayout.getHash()};

        auto it = _psoCache.find(key);
        if (it != _psoCache.end())
            return it->second.Get();

        D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = {};
        psoDesc.pRootSignature = shader->getRootSignature();
        psoDesc.VS = shader->getVSBytecode();
        psoDesc.PS = shader->getPSBytecode();
        const auto& d3dLayout = vertexLayout.getD3DLayout();
        const auto& shaderLayout = shader->getInputLayout();
        psoDesc.InputLayout = {d3dLayout.data(), (UINT)d3dLayout.size() };

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
            log::error("PSOFactory::get: Failed to create Graphics PSO.");
            return nullptr;
        }

        _psoCache[key] = pso;
        return pso.Get();
	}


    std::size_t PSOKeyHasher ::operator()(const PSOKey& key) const 
    {
        // 1. Хешируем ShaderHandle
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
