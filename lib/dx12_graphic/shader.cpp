#include "pch.h"
#include "shader.h"
#include "cstdmf/string_utils.h"
#include "renderer.h"
#include "resource_manager.h"

#include <d3dcompiler.h>
#include <algorithm>
#include <regex>
#include <filesystem>



#pragma comment(lib, "d3dcompiler.lib")

namespace
{
    std::string toUpper(std::string s) {
        std::transform(s.begin(), s.end(), s.begin(),
            [](unsigned char c) { return std::toupper(c); });
        return s;
    }
    struct ReflectionData
    {
        std::unordered_map<std::string, csyren::render::ShaderResourceInfo>& resourceMap;
        std::unordered_map<std::string, csyren::render::ConstantBufferVariableInfo>& variableInfoMap;
        std::unordered_map<std::string, UINT>& constantBufferSizes;
        std::vector<D3D12_ROOT_PARAMETER1>& rootParameters;
        std::vector<std::unique_ptr<D3D12_DESCRIPTOR_RANGE1[]>>& descriptorRanges;
        std::unordered_map<UINT, D3D12_STATIC_SAMPLER_DESC>& samplerMap;
    };

   void reflectShaderStage(
       ID3D12ShaderReflection* reflection,
       D3D12_SHADER_VISIBILITY visibility,
       ReflectionData& data)
   {
       D3D12_SHADER_DESC shaderDesc;
       reflection->GetDesc(&shaderDesc);

       // 1. Рефлексия связанных ресурсов (CBV, SRV, Samplers)
       for (UINT i = 0; i < shaderDesc.BoundResources; ++i)
       {
           D3D12_SHADER_INPUT_BIND_DESC bindDesc;
           reflection->GetResourceBindingDesc(i, &bindDesc);

           std::string resourceName = bindDesc.Name;
           auto it = data.resourceMap.find(resourceName);

           if (it != data.resourceMap.end())
           {
               // Ресурс уже был найден в другой стадии. Просто повышаем видимость.
               UINT rootParamIndex = it->second.rootParameterIndex;
               data.rootParameters[rootParamIndex].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

               // Для сэмплеров делаем то же самое
               if (bindDesc.Type == D3D_SIT_SAMPLER) {
                   auto samp_it = data.samplerMap.find(bindDesc.BindPoint);
                   if (samp_it != data.samplerMap.end()) {
                       samp_it->second.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
                   }
               }
               continue;
           }

           csyren::render::ShaderResourceInfo info;
           info.name = resourceName;
           info.shaderRegister = bindDesc.BindPoint;
           info.registerSpace = bindDesc.Space;
           info.rootParameterIndex = static_cast<UINT>(data.rootParameters.size());

           D3D12_ROOT_PARAMETER1 param = {};
           param.ShaderVisibility = visibility;

           switch (bindDesc.Type)
           {
           case D3D_SIT_CBUFFER:
           {
               param.ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
               param.Descriptor.ShaderRegister = bindDesc.BindPoint;
               param.Descriptor.RegisterSpace = bindDesc.Space;
               param.Descriptor.Flags = D3D12_ROOT_DESCRIPTOR_FLAG_DATA_STATIC;
               break;
           }
           case D3D_SIT_TEXTURE:
           case D3D_SIT_STRUCTURED:
           case D3D_SIT_TBUFFER:
           {
               param.ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
               auto newRange = std::make_unique<D3D12_DESCRIPTOR_RANGE1[]>(1);
               newRange[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
               newRange[0].NumDescriptors = 1;
               newRange[0].BaseShaderRegister = bindDesc.BindPoint;
               newRange[0].RegisterSpace = bindDesc.Space;
               newRange[0].OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;
               newRange[0].Flags = D3D12_DESCRIPTOR_RANGE_FLAG_DATA_STATIC;

               param.DescriptorTable.NumDescriptorRanges = 1;
               param.DescriptorTable.pDescriptorRanges = newRange.get();
               data.descriptorRanges.push_back(std::move(newRange));
               break;
           }
           case D3D_SIT_SAMPLER:
           {
               D3D12_STATIC_SAMPLER_DESC samplerDesc = {};
               samplerDesc.Filter = D3D12_FILTER_ANISOTROPIC;
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
               data.samplerMap[bindDesc.BindPoint] = samplerDesc;
               continue;
           }
           default:
               continue;
           }

           data.rootParameters.push_back(param);
           data.resourceMap[resourceName] = info;
       }

       for (UINT i = 0; i < shaderDesc.ConstantBuffers; ++i)
       {
           ID3D12ShaderReflectionConstantBuffer* cbuffer = reflection->GetConstantBufferByIndex(i);
           D3D12_SHADER_BUFFER_DESC cbDesc;
           cbuffer->GetDesc(&cbDesc);

           if (data.constantBufferSizes.count(cbDesc.Name)) continue;

           data.constantBufferSizes[cbDesc.Name] = cbDesc.Size;

           for (UINT j = 0; j < cbDesc.Variables; ++j)
           {
               ID3D12ShaderReflectionVariable* var = cbuffer->GetVariableByIndex(j);
               D3D12_SHADER_VARIABLE_DESC varDesc;
               var->GetDesc(&varDesc);

               ID3D12ShaderReflectionType* varType = var->GetType();
               D3D12_SHADER_TYPE_DESC typeDesc;
               varType->GetDesc(&typeDesc);

               csyren::render::ConstantBufferVariableInfo varInfo;
               varInfo.bufferName = cbDesc.Name;
               varInfo.offset = varDesc.StartOffset;
               varInfo.size = varDesc.Size;
               varInfo.needsTranspose = typeDesc.Class == D3D_SVC_MATRIX_COLUMNS;

               data.variableInfoMap[varDesc.Name] = std::move(varInfo);
           }
       }
   }

}

namespace csyren::render
{
	bool Shader::finalizeInit(Renderer& renderer, const D3D12_SHADER_BYTECODE& vs,const D3D12_SHADER_BYTECODE& ps)
	{
        auto device = renderer.device();

        if (!validateMeta(vs, ps))
        {
            log::error("Shader: Metadata validation failed.");
            return false;
        }

        if (!buildRootSignatureFromReflection(device, vs, ps))
        {
            log::error("Shader::init : cant read reflection from shader.");
            return false;
        }

        if (!buildInputLayoutFromReflection(vs))
        {
            log::error("Shader::init : failed to build Input Layout from reflection.");
            return false;
        }

        linkSemantics();
        buildSemanticLayout();
        log::info("Shader : initialized successfully.");

        return true;

	}

    Microsoft::WRL::ComPtr<ID3DBlob> Shader::compileShader(const std::string& source, const char* target, const std::string& entryPoint,bool ignoreMissingEntryPoint = false)
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
            log::error("Shader compilation {}", (char*)errors->GetBufferPointer());
        }
        if (FAILED(hr)) 
        {
            return nullptr;
        }
        return byteCode;
    }

    bool Shader::init(Renderer& renderer,from_source_code_t, const std::string& code)
    {
        return compileAndInit(renderer,code,"");
    }

    bool Shader::init(Renderer& renderer,from_asset_path_t, const std::string& filepath)
    {
        return init(renderer,filepath);
    }

    bool Shader::init(Renderer& renderer, const std::string& assetPath)
    {
        std::filesystem::path relativePath(assetPath);

#if defined(_DEBUG)

        std::string sourcePath = (std::filesystem::path("assets") / relativePath).string();
        std::string shaderCode = cstdmf::loadStringFromFile(sourcePath);
        if (shaderCode.empty())
        {
            log::error("Shader::init: Source file not found in DEBUG mode: {}", assetPath);
            return false;
        }
        return compileAndInit(renderer, shaderCode, relativePath);
#else
        return loadPrecompiledAndInit(renderer, relativePath);
#endif
    }

    bool Shader::compileAndInit(Renderer& renderer, const std::string& shaderCode, const std::filesystem::path relativePath)
    {
        auto path = relativePath.string();
        log::info("Shader {}: Compiling on the fly...", path);
        constexpr bool INGORE_MISSING_SHADER_STEP = true;

        _vsBlob = compileShader(shaderCode, "vs_5_1", "VSMain");
        _psBlob = compileShader(shaderCode, "ps_5_1", "PSMain");
        _gsBlob = compileShader(shaderCode, "gs_5_1", "GSMain", INGORE_MISSING_SHADER_STEP);
        _hsBlob = compileShader(shaderCode, "hs_5_1", "HSMain", INGORE_MISSING_SHADER_STEP);
        _dsBlob = compileShader(shaderCode, "ds_5_1", "DSMain", INGORE_MISSING_SHADER_STEP);

        _meta = ShaderMetaBuilder::build(shaderCode);
        if (!_meta)
        {
            log::error("Shader {}: Failed to build metadata from source", path);
            return false;
        }

        if (!path.empty())
        {
            std::filesystem::path buildPath = std::filesystem::path("build") / relativePath;
            std::filesystem::path buildDir = buildPath.parent_path();
            std::filesystem::create_directories(buildDir);

            std::filesystem::path metaPath = buildPath;
            metaPath.replace_extension("json");
            ShaderMetaBuilder::save(*_meta, metaPath.string());

            // universal shader stage saver
            std::vector<std::pair<ID3DBlob*, std::string>> stages = {
                { _vsBlob.Get(), "_vs.cso" },
                { _psBlob.Get(), "_ps.cso" },
                { _gsBlob.Get(), "_gs.cso" },
                { _hsBlob.Get(), "_hs.cso" },
                { _dsBlob.Get(), "_ds.cso" }
            };

            for (auto& [blob, suffix] : stages)
            {
                if (!blob)
                    continue; // optional stage (missing entry point)

                std::filesystem::path blobPath = buildPath;
                blobPath.replace_extension(suffix);

                HRESULT hr = D3DWriteBlobToFile(blob, blobPath.c_str(), TRUE);
                if (FAILED(hr))
                {
                    log::error("Failed to save shader blob {}", blobPath.string());
                }
            }

            //TODO save opitional shader steps
            log::info("Shader {}: Build shader saved.", path);
        }
        return finalizeInit(renderer, getVSBytecode(), getPSBytecode());
    }

    bool Shader::loadPrecompiledAndInit(Renderer& renderer, const std::filesystem::path& relativePath)
    {
        log::info("Shader {} : loading pre compiled data...", relativePath.string());
        std::filesystem::path buildPath = std::filesystem::path("build") / relativePath;

        std::filesystem::path metaPath = buildPath;
        metaPath.replace_extension("json");

        std::ifstream metaFile(metaPath);

        if (!metaFile.is_open())
        {
            log::error("Shader : Could not load metadata file: {}", metaPath.string());
            return false;
        }

        _meta = ShaderMetaBuilder::build(nlohmann::json::parse(metaFile));
        if (!_meta)
        {
            log::error("Shader : Filed to build metadata from file {}.", metaPath.string());
            return false;
        }

        std::filesystem::path vsBlobPath = buildPath;
        vsBlobPath.replace_extension("_vs.cso");

        if (DX_LOG(D3DReadFileToBlob(vsBlobPath.c_str(), &_vsBlob)))
        {
            return false;
        }

        std::filesystem::path psBlobPath = buildPath;
        psBlobPath.replace_extension("_ps.cso");
        if (DX_LOG(D3DReadFileToBlob(psBlobPath.c_str(), &_psBlob)))
        {
            return false;
        }

        D3D12_SHADER_BYTECODE vs = { _vsBlob->GetBufferPointer(), _vsBlob->GetBufferSize() };
        D3D12_SHADER_BYTECODE ps = { _psBlob->GetBufferPointer(), _psBlob->GetBufferSize() };
        return finalizeInit(renderer, vs, ps);

    }


	UINT Shader::getRootParameterIndex(const std::string& resourceName) const
	{
		auto it = _resourceMap.find(resourceName);
		if (it == _resourceMap.end()) return UINT_MAX;

		return it->second.rootParameterIndex;
	}


	bool Shader::buildRootSignatureFromReflection(ID3D12Device* device, const D3D12_SHADER_BYTECODE& vs, const D3D12_SHADER_BYTECODE& ps)
	{
        Microsoft::WRL::ComPtr<ID3D12ShaderReflection> vsReflection, psReflection;
        if (DX_FAILED(D3DReflect(vs.pShaderBytecode, vs.BytecodeLength, IID_PPV_ARGS(&vsReflection))))
        {
            return false;
        }
        if (DX_FAILED(D3DReflect(ps.pShaderBytecode, ps.BytecodeLength, IID_PPV_ARGS(&psReflection))))
        {
            return false;
        }
        std::vector<D3D12_ROOT_PARAMETER1> rootParameters;
        std::vector<std::unique_ptr<D3D12_DESCRIPTOR_RANGE1[]>> descriptorRanges;
        std::unordered_map<UINT, D3D12_STATIC_SAMPLER_DESC> samplerMap; // register -> desc

        _resourceMap.clear();
        _variableInfoMap.clear();
        _constantBufferSizes.clear();

        ReflectionData data
        {
            _resourceMap,
            _variableInfoMap,
            _constantBufferSizes,
            rootParameters,
            descriptorRanges,
            samplerMap
        };
        reflectShaderStage(vsReflection.Get(), D3D12_SHADER_VISIBILITY_VERTEX, data);
        reflectShaderStage(psReflection.Get(), D3D12_SHADER_VISIBILITY_PIXEL, data);



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
        _inputLayout.reserve(shaderDesc.InputParameters);
        _InputLayoutSemantic.reserve(shaderDesc.InputParameters);

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
            _InputLayoutSemantic.push_back(paramDesc.SemanticName);
            elementDesc.SemanticName = _InputLayoutSemantic.back().c_str(); //paramDesc.SemanticName exist in reflection heap and will be destroyed after method complete.
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
            std::string semanticNameUpper = toUpper(_InputLayoutSemantic.back());
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
                std::string semanticNameStr = _InputLayoutSemantic.back();
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
                   log::error("Shader::buildInputLayout: Could not deduce format for semantic '{}' with component mask{}. Aborting layout creation.", _InputLayoutSemantic.back(), paramDesc.Mask);
                   return false;
                }
            }
            elementDesc.Format = format;
            _inputLayout.push_back(elementDesc);
        }
        //_inputLayout[1].AlignedByteOffset = 12;
        return true;
    }

    #pragma optimize("",off)
    void Shader::linkSemantics()
    {
        _linkedBuffers.clear();
        if (!_meta) return;

        const auto& varialbeRegistry = details::EngineSemanticRegistry::instance();
        const auto& updateRegistry = details::EngineUpdateRegistry::instance();
        const auto& varView = _meta->variableView();
        for (const auto& cbufferMeta : _meta->cbufferView())
        {
            if (!_resourceMap.contains(cbufferMeta.name))
            {
                log::error("Shader::linkSemantics : Failed to find constant data '{}' in shader but meta file reference to it. different version between shader and meta files??", cbufferMeta.name);
                continue;
            }

            auto it = cbufferMeta.attributes.find("update");
            if (it == cbufferMeta.attributes.end()) continue;

            auto resourceInfoIt = _resourceMap.find(cbufferMeta.name);
            if (resourceInfoIt == _resourceMap.end())
            {
                log::warning("Shader::linkSemantics: CBuffer '{}' found in meta, but not reflected in shader. Skipping.", cbufferMeta.name);
                continue;
            }

            const std::string& updateName = it->second;
            const details::UpdateInfo* info = updateRegistry.find(updateName);

            
            if (info)//we know how to update this buffer
            {
                LinkedBuffer linkedBuffer;
                linkedBuffer.bufferName = cbufferMeta.name;
                linkedBuffer.type = info->type;
                linkedBuffer.size = _constantBufferSizes[cbufferMeta.name];
                linkedBuffer.rootParameterIndex = resourceInfoIt->second.rootParameterIndex;
                

                for (const auto& [name, shaderVarInfo] : _variableInfoMap)
                {
                    if (shaderVarInfo.bufferName != cbufferMeta.name)
                    {
                        continue;
                    }
                    std::string semantic;
                    for (auto& varMeta : varView)
                    {
                        if (varMeta.name == name)
                        {
                            auto it = varMeta.attributes.find("semantic");
                            if (it != varMeta.attributes.end())
                            {
                                semantic = it->second;
                            }
                        }
                    }

                    linkedBuffer.variables.emplace_back(LinkedVariable{ name,semantic,details::SemanticDataType::Unknown,shaderVarInfo.offset,shaderVarInfo.size,shaderVarInfo.needsTranspose });
                }
                if (!linkedBuffer.variables.empty())
                {
                    _linkedBuffers.emplace_back(std::move(linkedBuffer));
                }
            }
        }
    }

    void Shader::buildSemanticLayout()
    {
        _semanticLayouts.clear();

        const auto& semanticRegistry = details::EngineSemanticRegistry::instance();
        const std::string SEMANTIC_ATTR = "semantic";
        for (auto& linkedBuffer : _linkedBuffers)
        {
            SemanticBufferLayout layout;
            layout.type = linkedBuffer.type;
            layout.size = static_cast<uint32_t>(linkedBuffer.size);
            layout.rootParameterIndex = static_cast<uint32_t>(linkedBuffer.rootParameterIndex);
            const auto& varMetaView = _meta->variableView();
            for (auto& var : linkedBuffer.variables)
            {
                const auto& metaIt = std::find_if(varMetaView.begin(), varMetaView.end(), [&var](const auto& meta) {return meta.name == var.name; });
                if (metaIt == varMetaView.end())
                    continue;
                const auto& attrs = metaIt->attributes;

                auto semIt = attrs.find(SEMANTIC_ATTR);
                if (semIt == attrs.end())
                    continue;
                const auto& semName = semIt->second;

                const auto* sem = semanticRegistry.find(semName);
                if (!sem)
                    continue; //not an engine variable;

                layout.copyCommands.emplace_back(SemanticCopyCommand{
                    static_cast<uint32_t>(sem->offset),
                    static_cast<uint32_t>(var.offset),
                    static_cast<uint32_t>(sem->size),
                    var.needTranspose
                    });
            }

            if (!layout.copyCommands.empty())
                _semanticLayouts.push_back(std::move(layout));
        }
    }

    bool Shader::validateMeta(const D3D12_SHADER_BYTECODE& vs, const D3D12_SHADER_BYTECODE& ps)
    {
        log::warning("Shader : Metadata validation is not yet implemented.");
        return true;
    }

    const LinkedBuffer* Shader::getConstantBuffer(details::CBufferUpdateType updateType) const noexcept
    {
        auto it = std::find_if(_linkedBuffers.begin(), _linkedBuffers.end(), [updateType](const auto& buffer) {return updateType == buffer.type; });
        if (it == _linkedBuffers.end())
            return nullptr;
        return &(*it);
    }

    const SemanticBufferLayout* Shader::getSemanticBuffer(details::CBufferUpdateType updateType) const noexcept
    {
        auto it = std::find_if(_semanticLayouts.begin(), _semanticLayouts.end(), [updateType](const auto& buffer) {return updateType == buffer.type; });
        if (it == _semanticLayouts.end())
            return nullptr;
        return &(*it);
    }

}
