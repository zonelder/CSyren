#include "pch.h"
#include "shader_base.h"

#include <d3dcompiler.h>

#pragma comment(lib, "d3dcompiler.lib")


struct ResourceKey
{
    std::string name;
    uint32_t    space;
    uint32_t    reg;
    D3D_SHADER_INPUT_TYPE type;
    bool operator==(const ResourceKey& o) const noexcept = default;
};

namespace std
{
    template<> struct hash<ResourceKey>
    {
        size_t operator()(const ResourceKey& k) const noexcept
        {
            size_t h1 = hash<string>{}(k.name);
            size_t h2 = hash<uint32_t>{}(k.space) ^ (hash<uint32_t>{}(k.reg) << 1);
            return h1 ^ (h2 << 1) ^ (k.type << 31);
        }
    };
}

namespace
{
    struct ReflectionData
    {
        std::unordered_map<std::string, csyren::render::ShaderResourceInfo>& resourceMap;
        std::unordered_map<std::string, csyren::render::ConstantBufferVariableInfo>& variableInfoMap;
        std::unordered_map<std::string, UINT>& constantBufferSizes;
        std::vector<D3D12_ROOT_PARAMETER1>& rootParameters;
        std::vector<std::unique_ptr<D3D12_DESCRIPTOR_RANGE1[]>>& descriptorRanges;
        std::unordered_map<UINT, D3D12_STATIC_SAMPLER_DESC>& samplerMap;
    };


    static void collectResources(
        ID3D12ShaderReflection* refl,
        D3D12_SHADER_VISIBILITY stage,
        std::unordered_map<ResourceKey, D3D12_SHADER_VISIBILITY>& vis,
        std::unordered_map<std::string, uint32_t>& cbSizes,
        std::unordered_map<std::string, std::vector<std::pair<std::string, csyren::render::ConstantBufferVariableInfo>>>& vars)
    {
        D3D12_SHADER_DESC desc;
        refl->GetDesc(&desc);

        /* ----- 1. resources (CBV, SRV, UAV, sampler) ----- */
        for (UINT i = 0; i < desc.BoundResources; ++i)
        {
            D3D12_SHADER_INPUT_BIND_DESC bind;
            refl->GetResourceBindingDesc(i, &bind);

            ResourceKey key{ bind.Name, bind.Space, bind.BindPoint,
                             bind.Type };

            auto [it, inserted] = vis.try_emplace(key, stage);
            if (!inserted && it->second != stage)        // seen in another stage
                it->second = D3D12_SHADER_VISIBILITY_ALL;
        }

        /* ----- 2. constant-buffer introspection (kept for your editor) ----- */
        for (UINT i = 0; i < desc.ConstantBuffers; ++i)
        {
            auto* cb = refl->GetConstantBufferByIndex(i);
            D3D12_SHADER_BUFFER_DESC cbDesc;
            cb->GetDesc(&cbDesc);

            cbSizes[cbDesc.Name] = cbDesc.Size;

            auto& varArray = vars[cbDesc.Name];
            varArray.reserve(cbDesc.Variables);
            for (UINT v = 0; v < cbDesc.Variables; ++v)
            {
                auto* var = cb->GetVariableByIndex(v);
                D3D12_SHADER_VARIABLE_DESC vd;
                var->GetDesc(&vd);
                ID3D12ShaderReflectionType* t = var->GetType();
                D3D12_SHADER_TYPE_DESC td;
                t->GetDesc(&td);

                csyren::render::ConstantBufferVariableInfo info;
                info.bufferName = cbDesc.Name;
                info.offset = vd.StartOffset;
                info.size = vd.Size;
                info.needsTranspose = (td.Class == D3D_SVC_MATRIX_COLUMNS);
                varArray.emplace_back(std::make_pair(vd.Name, std::move(info)));
            }
        }
    }


}



namespace csyren::render
{

    Microsoft::WRL::ComPtr<ID3DBlob> ShaderBase::compileShader(const std::string& source, const char* target, const std::string& entryPoint, bool ignoreMissingEntryPoint)
    {
        UINT compileFlags = 0;
#if defined(_DEBUG)
        // compileFlags = D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
#endif
        Microsoft::WRL::ComPtr<ID3DBlob> byteCode;
        Microsoft::WRL::ComPtr<ID3DBlob> errors;
        HRESULT hr = D3DCompile(
            source.c_str(),
            source.length(),
            nullptr, nullptr, nullptr,
            entryPoint.c_str(),
            target,
            compileFlags, 0,
            &byteCode, &errors
        );

        //optional shader step
        if (ignoreMissingEntryPoint && FAILED(hr) && errors)
        {
            std::string msg = (char*)errors->GetBufferPointer();
            if (msg.find("X3501") != std::string::npos)
            {
                return nullptr;
            }
        }

        if (errors)
        {
            log::error("GraphicShader compilation {}", (char*)errors->GetBufferPointer());
        }
        if (FAILED(hr))
        {
            return nullptr;
        }
        return byteCode;
    }


    UINT ShaderBase::getRootParameterIndex(const std::string& resourceName) const
    {
        auto it = _resourceMap.find(resourceName);
        if (it == _resourceMap.end()) return UINT_MAX;

        return it->second.rootParameterIndex;
    }

    bool ShaderBase::buildRootSignatureFromReflection(
        ID3D12Device* device,
        const std::vector<std::pair<D3D12_SHADER_VISIBILITY,ID3DBlob*>>& shaderBlobs)
    {

        if (shaderBlobs.empty())
            return true;

        std::unordered_map<ResourceKey, D3D12_SHADER_VISIBILITY> resourceVisibility;
        std::unordered_map<std::string, uint32_t>                cbSizes;
        std::unordered_map<std::string, std::vector<std::pair<std::string, csyren::render::ConstantBufferVariableInfo>>> cbVars;

        std::vector< std::pair< D3D12_SHADER_VISIBILITY, Microsoft::WRL::ComPtr<ID3D12ShaderReflection>>> refls;

        for (const auto& [visibility, blob] : shaderBlobs)
        {
            if (!blob)
                continue;

            Microsoft::WRL::ComPtr<ID3D12ShaderReflection> refl;

            if (FAILED(D3DReflect(blob->GetBufferPointer(), blob->GetBufferSize(), IID_PPV_ARGS(&refl))))
            {
                log::error("buildRootSignatureFromReflection :Failed to read reflection data from shader blob with shader type {} ",static_cast<int>(visibility));
                return false;
            }
            refls.emplace_back(visibility, refl);
        }

        log::debug("GraphicShader:: collect resources from shader.");

        for (const auto& [visibility, refl] : refls)
        {
            collectResources(refl.Get(), visibility,
                resourceVisibility, cbSizes, cbVars);
        }

        std::vector<std::pair<ResourceKey, D3D12_SHADER_VISIBILITY>> items;
        items.reserve(resourceVisibility.size());
        for (auto& kv : resourceVisibility)
            items.push_back(kv);

        std::sort(items.begin(), items.end(),
            [](auto& a, auto& b)
            {
                // sort by space, then register, then type
                if (a.first.space != b.first.space)
                    return a.first.space < b.first.space;
                if (a.first.reg != b.first.reg)
                    return a.first.reg < b.first.reg;
                return a.first.type < b.first.type;
            });

        /* ---------- build root parameters ---------- */
        std::vector<D3D12_ROOT_PARAMETER1>   rootParams;
        std::vector<D3D12_STATIC_SAMPLER_DESC> staticSamplers;
        std::vector<D3D12_DESCRIPTOR_RANGE1>   ranges;

        rootParams.reserve(resourceVisibility.size());
        ranges.reserve(resourceVisibility.size());
        log::debug("GraphicShader: iterate over collected resources");
        for (auto& [key, vis] : items)
        {
            D3D12_ROOT_PARAMETER1 p = {};
            p.ShaderVisibility = vis;

            csyren::render::ShaderResourceInfo info;
            info.name = key.name;
            info.shaderRegister = key.reg;
            info.registerSpace = key.space;
            info.rootParameterIndex = static_cast<UINT>(rootParams.size());
            _resourceMap[info.name] = std::move(info);

            switch (key.type)        // crude but fast way to know the type
            {                           // (you can store the real type in key)
            case D3D_SIT_CBUFFER:                   // b = CBV
                p.ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
                p.Descriptor.ShaderRegister = key.reg;
                p.Descriptor.RegisterSpace = key.space;
                p.Descriptor.Flags = D3D12_ROOT_DESCRIPTOR_FLAG_DATA_STATIC;
                break;

            case D3D_SIT_TEXTURE:            // обычные SRV
            case D3D_SIT_STRUCTURED:         // structured buffer (через SRV)
            case D3D_SIT_BYTEADDRESS:        // RAW buffer (через SRV)
            {
                ranges.emplace_back();
                auto& r = ranges.back();
                r.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
                r.NumDescriptors = 1;
                r.BaseShaderRegister = key.reg;
                r.RegisterSpace = key.space;
                r.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

                p.ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
                p.DescriptorTable.NumDescriptorRanges = 1;
                p.DescriptorTable.pDescriptorRanges = &r;
                break;
            }
            case D3D_SIT_SAMPLER:
            {
                D3D12_STATIC_SAMPLER_DESC s = {};
                s.Filter = D3D12_FILTER_ANISOTROPIC;
                s.AddressU = s.AddressV = s.AddressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
                s.MaxAnisotropy = 16;
                s.ComparisonFunc = D3D12_COMPARISON_FUNC_ALWAYS;
                s.MinLOD = 0;
                s.MaxLOD = D3D12_FLOAT32_MAX;
                s.ShaderRegister = key.reg;
                s.RegisterSpace = key.space;
                s.ShaderVisibility = vis;
                staticSamplers.push_back(s);
                continue; // sampler не добавляет root param
            }
            case D3D_SIT_UAV_RWTYPED:
            case D3D_SIT_UAV_RWSTRUCTURED:
            case D3D_SIT_UAV_RWBYTEADDRESS:
            case D3D_SIT_UAV_APPEND_STRUCTURED:
            case D3D_SIT_UAV_CONSUME_STRUCTURED:
            case D3D_SIT_UAV_RWSTRUCTURED_WITH_COUNTER:
            {
                ranges.emplace_back();
                auto& r = ranges.back();
                r.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
                r.NumDescriptors = 1;
                r.BaseShaderRegister = key.reg;
                r.RegisterSpace = key.space;
                r.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

                p.ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
                p.DescriptorTable.NumDescriptorRanges = 1;
                p.DescriptorTable.pDescriptorRanges = &r;
                break;
            }
            default: continue;
            }
            rootParams.push_back(p);
        }

        D3D12_VERSIONED_ROOT_SIGNATURE_DESC desc = {};
        desc.Version = D3D_ROOT_SIGNATURE_VERSION_1_1;
        desc.Desc_1_1.NumParameters = static_cast<UINT>(rootParams.size());
        desc.Desc_1_1.pParameters = rootParams.data();
        desc.Desc_1_1.NumStaticSamplers = static_cast<UINT>(staticSamplers.size());
        desc.Desc_1_1.pStaticSamplers = staticSamplers.data();
        desc.Desc_1_1.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

        Microsoft::WRL::ComPtr<ID3DBlob> blob, err;
        if (DX_FAILED(D3D12SerializeVersionedRootSignature(&desc, &blob, &err)))
        {
            log::error("Serialize failed: {}", err ? (char*)err->GetBufferPointer() : "unknown");
            return false;
        }
        if (DX_FAILED(device->CreateRootSignature(
            0, blob->GetBufferPointer(), blob->GetBufferSize(),
            IID_PPV_ARGS(&_rootSignature))))
        {
            log::error("GraphicShader: Failed to created root signature.");
            return false;
        }
        _constantBufferSizes = std::move(cbSizes);

        //todo - move semantic;
        for (const auto& [_, cb] : cbVars)
        {
            for (const auto& [var_name, var] : cb)
            {
                _variableInfoMap[var_name] = var;
            }

        }
        return true;
    }
}