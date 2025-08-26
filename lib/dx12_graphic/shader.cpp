#include "pch.h"
#include "shader.h"
#include "cstdmf/string_utils.h"

#include <d3dcompiler.h>
#include <algorithm>

#pragma comment(lib, "d3dcompiler.lib")

//TODO:
//1) read statis samplers via reflection

namespace
{
    std::string toUpper(std::string s) {
        std::transform(s.begin(), s.end(), s.begin(),
            [](unsigned char c) { return std::toupper(c); });
        return s;
    }


    using ShaderResourceInfo = csyren::render::ShaderResourceInfo;
    void reflectShader(
        const D3D12_SHADER_BYTECODE& shaderBytecode,
        D3D12_SHADER_VISIBILITY visibility,
        std::unordered_map<std::string, ShaderResourceInfo>& resourceMap,
        std::vector<D3D12_ROOT_PARAMETER1>& rootParameters,
        std::vector<std::unique_ptr<D3D12_DESCRIPTOR_RANGE1[]>>& descriptorRanges,
        std::unordered_map<UINT, D3D12_STATIC_SAMPLER_DESC>& samplerMap) // register -> desc)
    {
        Microsoft::WRL::ComPtr<ID3D12ShaderReflection> reflection;
        HRESULT hr = D3DReflect(shaderBytecode.pShaderBytecode, shaderBytecode.BytecodeLength, IID_PPV_ARGS(&reflection));
        if (FAILED(hr))
        {
            throw std::runtime_error("D3DReflect failed. Shader bytecode might be invalid.");
        }

        D3D12_SHADER_DESC shaderDesc;
        reflection->GetDesc(&shaderDesc);

        for (UINT i = 0; i < shaderDesc.BoundResources; ++i)
        {
            D3D12_SHADER_INPUT_BIND_DESC bindDesc;
            reflection->GetResourceBindingDesc(i, &bindDesc);

            std::string resourceName = bindDesc.Name;
            auto it = resourceMap.find(resourceName);
            //TODO it can be that in ps and vs different resources are named equal. has to check this.
            if (it != resourceMap.end())
            {
                auto index = it->second.rootParameterIndex;
                D3D12_ROOT_PARAMETER_TYPE existingType = rootParameters[index].ParameterType;
                bool isExistingSrv = (existingType == D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE);
                bool isCurrentSrv = (bindDesc.Type == D3D_SIT_TEXTURE || bindDesc.Type == D3D_SIT_STRUCTURED);

                if ((existingType == D3D12_ROOT_PARAMETER_TYPE_CBV && bindDesc.Type == D3D_SIT_CBUFFER) ||
                    (isExistingSrv && isCurrentSrv))
                {
                    // Типы совпадают, просто обновляем видимость
                    rootParameters[index].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
                }
                else
                {
                    // ОШИБКА: Ресурс с тем же именем, но разным типом!
                    csyren::log::error("Shader reflection error: Resource {} is defined with different types in vertex and pixel shaders.", resourceName);
                    continue;
                }
            }
            else
            {
                ShaderResourceInfo info;
                info.name = resourceName;
                info.shaderRegister = bindDesc.BindPoint;
                info.registerSpace = bindDesc.Space;
                info.rootParameterIndex = static_cast<UINT>(rootParameters.size());

                D3D12_ROOT_PARAMETER1 param = {};
                param.ShaderVisibility = visibility;

                switch (bindDesc.Type)
                {
                case D3D_SIT_CBUFFER:
                    param.ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
                    param.Descriptor.ShaderRegister = bindDesc.BindPoint;
                    param.Descriptor.RegisterSpace = bindDesc.Space;
                    param.Descriptor.Flags = D3D12_ROOT_DESCRIPTOR_FLAG_DATA_STATIC;
                    break;

                case D3D_SIT_TEXTURE:
                case D3D_SIT_STRUCTURED:
                case D3D_SIT_TBUFFER:
                    param.ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
                    {
                        auto newRange = std::make_unique<D3D12_DESCRIPTOR_RANGE1[]>(1);
                        newRange[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
                        newRange[0].NumDescriptors = bindDesc.BindCount;
                        newRange[0].BaseShaderRegister = bindDesc.BindPoint;
                        newRange[0].RegisterSpace = bindDesc.Space;
                        newRange[0].OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;
                        newRange[0].Flags = D3D12_DESCRIPTOR_RANGE_FLAG_DATA_STATIC;

                        param.DescriptorTable.NumDescriptorRanges = 1;
                        param.DescriptorTable.pDescriptorRanges = newRange.get();
                        descriptorRanges.push_back(std::move(newRange));
                    }
                    break;
                case D3D_SIT_SAMPLER:
                {
                    auto it = samplerMap.find(bindDesc.BindPoint);
                    if (it != samplerMap.end()) {
                        it->second.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
                    }
                    else
                    {
                        D3D12_STATIC_SAMPLER_DESC samplerDesc = {};
                        samplerDesc.Filter = D3D12_FILTER_ANISOTROPIC; // Дефолтная настройка
                        samplerDesc.AddressU = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
                        samplerDesc.AddressV = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
                        samplerDesc.AddressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
                        samplerDesc.MaxAnisotropy = 16;
                        samplerDesc.ComparisonFunc = D3D12_COMPARISON_FUNC_ALWAYS;
                        samplerDesc.MinLOD = 0.0f;
                        samplerDesc.MaxLOD = D3D12_FLOAT32_MAX;
                        samplerDesc.ShaderRegister = bindDesc.BindPoint;
                        samplerDesc.RegisterSpace = bindDesc.Space;
                        samplerDesc.ShaderVisibility = visibility;
                        samplerMap[bindDesc.BindPoint] = samplerDesc;
                    }
                    continue;
                }
                default:
                    continue;
                }

                rootParameters.push_back(param);
                resourceMap[resourceName] = info;
            }
        }
    }

}

namespace csyren::render
{
	bool Shader::init(ID3D12Device* device, const Microsoft::WRL::ComPtr< ID3DBlob> vsBlob, const Microsoft::WRL::ComPtr< ID3DBlob> psBlob,const std::wstring& name)
	{
        if (!vsBlob || !psBlob)
        {
            log::error("Shader::init: Provided shader blobs are null.");
            return false;
        }

        D3D12_SHADER_BYTECODE vs = { vsBlob->GetBufferPointer(), vsBlob->GetBufferSize() };
        D3D12_SHADER_BYTECODE ps = { psBlob->GetBufferPointer(), psBlob->GetBufferSize() };

        if (!buildRootSignatureFromReflection(device, vs, ps))
        {
            log::error("Shader::init() : cant read reflection from shader.");
            return false;
        }

        if (!buildInputLayoutFromReflection(vs))
        {
            log::error("Shader::init() : failed to build Input Layout from reflection.");
            return false;
        }

        _vsBlob = vsBlob;
        _psBlob = psBlob;
        //TODO would be nice to set shader file name or similar.
        _rootSignature->SetName(name.c_str());
        return true;

	}
    bool Shader::init(ID3D12Device* device, const std::string& vsCode, const std::string& psCode, const std::wstring& name)
    {
        using Microsoft::WRL::ComPtr;

        UINT compileFlags = 0;
#if defined(_DEBUG)
        compileFlags = D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
#endif

        ComPtr<ID3DBlob> vsBlob;
        ComPtr<ID3DBlob> psBlob;
        ComPtr<ID3DBlob> errorBlob;

        // Компилируем вершинный шейдер
        HRESULT hr = D3DCompile(vsCode.c_str(), vsCode.length(), nullptr, nullptr, nullptr,
            "main", "vs_5_1", compileFlags, 0, &vsBlob, &errorBlob);

        if (DX_FAILED(hr))
        {
            if (errorBlob) 
            {
                log::error("Vertex shader compilation failed for '{}': {}",cstdmf::to_string(name), std::string((char*)errorBlob->GetBufferPointer()));
            }
            return false;
        }

        // Компилируем пиксельный шейдер
        hr = D3DCompile(psCode.c_str(), psCode.length(), nullptr, nullptr, nullptr,
            "main", "ps_5_1", compileFlags, 0, &psBlob, &errorBlob);

        if (DX_FAILED(hr))
        {
            if (errorBlob) 
            {
                log::error("Pixel shader compilation failed for '{}': {}",cstdmf::to_string(name), std::string((char*)errorBlob->GetBufferPointer()));
            }
            return false;
        }

        // Вызываем основной метод с готовым байт-кодом
        return init(device,vsBlob,psBlob,name);
    }

    bool Shader::init(ID3D12Device* device, const std::wstring& vsPath, const std::wstring& psPath)
    {
        using Microsoft::WRL::ComPtr;

        ComPtr<ID3DBlob> vsBlob;
        ComPtr<ID3DBlob> psBlob;

        HRESULT hr = D3DReadFileToBlob(vsPath.c_str(), &vsBlob);
        if (DX_FAILED(hr))
        {
            return false;
        }

        hr = D3DReadFileToBlob(psPath.c_str(), &psBlob);
        if (DX_FAILED(hr))
        {
            return false;
        }

        return init(device, vsBlob,psBlob,vsPath);
    }



	UINT Shader::getRootParameterIndex(const std::string& resourceName) const
	{
		auto it = _resourceMap.find(resourceName);
		if (it == _resourceMap.end()) return UINT_MAX;

		return it->second.rootParameterIndex;
	}

	bool Shader::buildRootSignatureFromReflection(ID3D12Device* device, const D3D12_SHADER_BYTECODE& vs, const D3D12_SHADER_BYTECODE& ps)
	{
        std::vector<D3D12_ROOT_PARAMETER1> rootParameters;
        std::vector<std::unique_ptr<D3D12_DESCRIPTOR_RANGE1[]>> descriptorRanges;
        std::unordered_map<UINT, D3D12_STATIC_SAMPLER_DESC> samplerMap; // register -> desc

        reflectShader(vs, D3D12_SHADER_VISIBILITY_VERTEX, _resourceMap, rootParameters, descriptorRanges, samplerMap);
        reflectShader(ps, D3D12_SHADER_VISIBILITY_PIXEL, _resourceMap, rootParameters, descriptorRanges, samplerMap);

        std::vector<D3D12_STATIC_SAMPLER_DESC> finalSamplers;
        for (const auto& pair : samplerMap) 
        {
            finalSamplers.push_back(pair.second);
        }
        // --- Шаг 2:serialize and create Root Signature ---
        D3D12_VERSIONED_ROOT_SIGNATURE_DESC rootSigDesc = {};
        rootSigDesc.Version = D3D_ROOT_SIGNATURE_VERSION_1_1;
        rootSigDesc.Desc_1_1.NumParameters = static_cast<UINT>(rootParameters.size());
        rootSigDesc.Desc_1_1.pParameters = rootParameters.empty() ? nullptr : rootParameters.data();
        rootSigDesc.Desc_1_1.NumStaticSamplers = static_cast<UINT>(finalSamplers.size());
        rootSigDesc.Desc_1_1.pStaticSamplers = finalSamplers.empty()? nullptr : finalSamplers.data();
        rootSigDesc.Desc_1_1.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

        Microsoft::WRL::ComPtr<ID3DBlob> signatureBlob;
        Microsoft::WRL::ComPtr<ID3DBlob> errorBlob;

        HRESULT hr = D3D12SerializeVersionedRootSignature(&rootSigDesc, &signatureBlob, &errorBlob);
        if (DX_FAILED(hr))
        {
            if (errorBlob) { log::error("D3D12SerializeVersionedRootSignature failed: {}", (char*)errorBlob->GetBufferPointer()); }
            return false;
        }

        hr = device->CreateRootSignature(0, signatureBlob->GetBufferPointer(), signatureBlob->GetBufferSize(), IID_PPV_ARGS(&_rootSignature));
        if (DX_FAILED(hr))
        {
            if (hr == DXGI_ERROR_DEVICE_REMOVED)
            {
                HRESULT reason = device->GetDeviceRemovedReason();
                // Поставьте здесь точку останова и посмотрите значение 'reason'.
                // Например, DXGI_ERROR_DEVICE_HUNG означает, что драйвер завис.
                log::error("Device removed! Reason: {:#x}", reason);
            }
            log::error("CreateRootSignature failed");
            return false;
        }
        return true;

	}

    bool Shader::buildInputLayoutFromReflection(const D3D12_SHADER_BYTECODE& vs)
    {
        Microsoft::WRL::ComPtr<ID3D12ShaderReflection> reflection;
        HRESULT hr = D3DReflect(vs.pShaderBytecode, vs.BytecodeLength, IID_PPV_ARGS(&reflection));
        if (DX_FAILED(hr))
        {
            return false;
        }
        D3D12_SHADER_DESC shaderDesc;
        reflection->GetDesc(&shaderDesc);
        _inputLayout.clear();
        _semanticNames.clear();
        _inputLayout.reserve(shaderDesc.InputParameters);
        _semanticNames.reserve(shaderDesc.InputParameters);

        for (UINT i = 0; i < shaderDesc.InputParameters; ++i)
        {
            D3D12_SIGNATURE_PARAMETER_DESC paramDesc;
            reflection->GetInputParameterDesc(i, &paramDesc);

            switch (paramDesc.SystemValueType)
            {
            case D3D_NAME_VERTEX_ID:
            case D3D_NAME_INSTANCE_ID:
            case D3D_NAME_PRIMITIVE_ID:
                //this values generate by input assembler and do not exist in vertex buffer.
                continue;

            case D3D_NAME_UNDEFINED:
            case D3D_NAME_POSITION: // POSITION/SV_POSITION - buffer data.
            default:
                break;
            }

            D3D12_INPUT_ELEMENT_DESC elementDesc = {};
            _semanticNames.push_back(paramDesc.SemanticName);
            elementDesc.SemanticName = _semanticNames.back().c_str(); //paramDesc.SemanticName exist in reflection heap and will be destroyed after method complete.
            elementDesc.SemanticIndex = paramDesc.SemanticIndex;
            elementDesc.InputSlot = 0; //one vertex buffer.
            elementDesc.AlignedByteOffset = (_inputLayout.empty()) ? 0 : D3D12_APPEND_ALIGNED_ELEMENT;
            elementDesc.InputSlotClass = D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA;
            elementDesc.InstanceDataStepRate = 0;
            // Mask (paramDesc.Mask) - is a bitfield.
            // 1 (0001) -> x
            // 3 (0011) -> xy
            // 7 (0111) -> xyz
            // 15 (1111) -> xyzw
            // we cout components by counting bits.

            UINT componentCount = 0;
            BYTE mask = paramDesc.Mask;
            while (mask > 0)
            {
                if (mask & 1) componentCount++;
                mask >>= 1;
            }
            std::string semanticNameUpper = toUpper(_semanticNames.back());
            DXGI_FORMAT format = DXGI_FORMAT_UNKNOWN;
            if (semanticNameUpper.find("POSITION") != std::string::npos) {
                if (componentCount == 3) format = DXGI_FORMAT_R32G32B32_FLOAT;
                else if (componentCount == 4) format = DXGI_FORMAT_R32G32B32A32_FLOAT; // Например, для гомогенных координат
            }
            else if (semanticNameUpper.find("NORMAL") != std::string::npos || semanticNameUpper.find("TANGENT") != std::string::npos) {
                if (componentCount == 4) format = DXGI_FORMAT_R10G10B10A2_UNORM;
                else if (componentCount == 3) format = DXGI_FORMAT_R32G32B32_FLOAT;
            }
            else if (semanticNameUpper.find("TEXCOORD") != std::string::npos) 
            {
                if (componentCount == 2) format = DXGI_FORMAT_R16G16_FLOAT;
                else if (componentCount == 1) format = DXGI_FORMAT_R16_FLOAT;
                else if (componentCount == 3) format = DXGI_FORMAT_R16G16B16A16_FLOAT;
                else if (componentCount == 4) format = DXGI_FORMAT_R16G16B16A16_FLOAT;
            }
            else if (semanticNameUpper.find("COLOR") != std::string::npos) 
            {
               format = DXGI_FORMAT_R8G8B8A8_UNORM;
            }

            //fallback to basic read.
            if (format == DXGI_FORMAT_UNKNOWN)
            {
                std::string semanticNameStr = _semanticNames.back();
                if (paramDesc.ComponentType == D3D_REGISTER_COMPONENT_FLOAT32)
                {
                    switch (componentCount) {
                    case 1: format = DXGI_FORMAT_R32_FLOAT; break;
                    case 2: format = DXGI_FORMAT_R32G32_FLOAT; break;
                    case 3: format = DXGI_FORMAT_R32G32B32_FLOAT; break;
                    case 4: format = DXGI_FORMAT_R32G32B32A32_FLOAT; break;
                    }
                }
                else if (paramDesc.ComponentType == D3D_REGISTER_COMPONENT_UINT32)
                {
                    switch (componentCount) {
                    case 1: format = DXGI_FORMAT_R32_UINT; break;
                    case 2: format = DXGI_FORMAT_R32G32_UINT; break;
                    case 3: format = DXGI_FORMAT_R32G32B32_UINT; break;
                    case 4: format = DXGI_FORMAT_R32G32B32A32_UINT; break;
                    }
                }
                else if (paramDesc.ComponentType == D3D_REGISTER_COMPONENT_SINT32)
                {
                    switch (componentCount) {
                    case 1: format = DXGI_FORMAT_R32_SINT; break;
                    case 2: format = DXGI_FORMAT_R32G32_SINT; break;
                    case 3: format = DXGI_FORMAT_R32G32B32_SINT; break;
                    case 4: format = DXGI_FORMAT_R32G32B32A32_SINT; break;
                    }
                }
                else 
                {
                   log::error("Shader::buildInputLayout: Could not deduce format for semantic '{}' with component mask{}. Aborting layout creation.", _semanticNames.back(), paramDesc.Mask);
                   return false;
                }
            }
            elementDesc.Format = format;
            _inputLayout.push_back(elementDesc);
        }
        //_inputLayout[1].AlignedByteOffset = 12;
        return true;
    }
}
