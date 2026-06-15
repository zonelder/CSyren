#include "pch.h"
#include "shader.h"
#include "cstdmf/string_utils.h"
#include "renderer.h"
#include "resource_manager.h"
#include "cstdmf/string_utils.h"

#include <algorithm>
#include <regex>
#include <filesystem>


namespace csyren::render
{
	bool GraphicShader::finalizeInit(ID3D12Device* device)
	{
        std::vector<std::pair<D3D12_SHADER_VISIBILITY, ID3DBlob*>> blobs;
        blobs.reserve(6);

        blobs.emplace_back(D3D12_SHADER_VISIBILITY_VERTEX,   _vsBlob.Get());
        blobs.emplace_back(D3D12_SHADER_VISIBILITY_PIXEL,    _psBlob.Get());
        blobs.emplace_back(D3D12_SHADER_VISIBILITY_DOMAIN,   _dsBlob.Get());
        blobs.emplace_back(D3D12_SHADER_VISIBILITY_HULL,     _hsBlob.Get());
        blobs.emplace_back(D3D12_SHADER_VISIBILITY_GEOMETRY, _gsBlob.Get());

        if (!buildRootSignatureFromReflection(device, blobs))
        {
            log::error("GraphicShader::init : cant read reflection from shader.");
            return false;
        }

        if (!buildInputLayoutFromReflection())
        {
            log::error("GraphicShader::init : failed to build Input Layout from reflection.");
            return false;
        }

        linkSemantics();
        buildSemanticLayout();
        log::info("GraphicShader : initialized successfully.");

        return true;

	}

    bool GraphicShader::init(from_source_code_t, const std::string& code)
    {
        return compileAndInit(code,"");
    }

    bool GraphicShader::init(from_asset_path_t, const std::string& filepath)
    {
        return init(filepath);
    }

    bool GraphicShader::init(const std::string& assetPath)
    {
        std::filesystem::path relativePath(assetPath);

#if defined(_DEBUG)

        std::string sourcePath = (std::filesystem::path("assets") / relativePath).string();
        std::string shaderCode = cstdmf::loadStringFromFile(sourcePath);
        if (shaderCode.empty())
        {
            log::error("GraphicShader::init: Source file not found in DEBUG mode: {}", assetPath);
            return false;
        }
        return compileAndInit(shaderCode, relativePath);
#else
        return loadPrecompiledAndInit(relativePath);
#endif
    }

    bool GraphicShader::compileAndInit(const std::string& shaderCode, const std::filesystem::path relativePath)
    {
        Renderer& renderer = *core::Services::get<Renderer>();
        auto path = relativePath.string();
        log::info("GraphicShader {}: Compiling on the fly...", path);
        constexpr bool INGORE_MISSING_SHADER_STEP = true;

        _vsBlob = compileShader(shaderCode, "vs_5_1", "VSMain");
        _psBlob = compileShader(shaderCode, "ps_5_1", "PSMain");
        _gsBlob = compileShader(shaderCode, "gs_5_1", "GSMain", INGORE_MISSING_SHADER_STEP);
        _hsBlob = compileShader(shaderCode, "hs_5_1", "HSMain", INGORE_MISSING_SHADER_STEP);
        _dsBlob = compileShader(shaderCode, "ds_5_1", "DSMain", INGORE_MISSING_SHADER_STEP);

        _meta = ShaderMetaBuilder::build(shaderCode);
        if (!_meta)
        {
            log::error("GraphicShader {}: Failed to build metadata from source", path);
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
            log::info("GraphicShader {}: Build shader saved.", path);
        }
        return finalizeInit(renderer.device());
    }

    bool GraphicShader::loadPrecompiledAndInit(const std::filesystem::path& relativePath)
    {
        Renderer& renderer = *core::Services::get<Renderer>();
        log::info("GraphicShader {} : loading pre compiled data...", relativePath.string());
        std::filesystem::path buildPath = std::filesystem::path("build") / relativePath;

        std::filesystem::path metaPath = buildPath;
        metaPath.replace_extension("json");

        std::ifstream metaFile(metaPath);

        if (!metaFile.is_open())
        {
            log::error("GraphicShader : Could not load metadata file: {}", metaPath.string());
            return false;
        }

        _meta = ShaderMetaBuilder::build(nlohmann::json::parse(metaFile));
        if (!_meta)
        {
            log::error("GraphicShader : Filed to build metadata from file {}.", metaPath.string());
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
        return finalizeInit(renderer.device());

    }

    bool GraphicShader::buildInputLayoutFromReflection()
    {
        Microsoft::WRL::ComPtr<ID3D12ShaderReflection> reflection;
        HRESULT hr = D3DReflect(_vsBlob->GetBufferPointer(), _vsBlob->GetBufferSize(), IID_PPV_ARGS(&reflection));
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
            std::string semanticNameUpper = cstdmf::toUpper(_InputLayoutSemantic.back());
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
                   log::error("GraphicShader::buildInputLayout: Could not deduce format for semantic '{}' with component mask{}. Aborting layout creation.", _InputLayoutSemantic.back(), paramDesc.Mask);
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
    void GraphicShader::linkSemantics()
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
                log::error("GraphicShader::linkSemantics : Failed to find constant data '{}' in shader but meta file reference to it. different version between shader and meta files??", cbufferMeta.name);
                continue;
            }

            auto it = cbufferMeta.attributes.find("update");
            if (it == cbufferMeta.attributes.end()) continue;

            auto resourceInfoIt = _resourceMap.find(cbufferMeta.name);
            if (resourceInfoIt == _resourceMap.end())
            {
                log::warning("GraphicShader::linkSemantics: CBuffer '{}' found in meta, but not reflected in shader. Skipping.", cbufferMeta.name);
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

    void GraphicShader::buildSemanticLayout()
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

    const LinkedBuffer* GraphicShader::getConstantBuffer(details::CBufferUpdateType updateType) const noexcept
    {
        auto it = std::find_if(_linkedBuffers.begin(), _linkedBuffers.end(), [updateType](const auto& buffer) {return updateType == buffer.type; });
        if (it == _linkedBuffers.end())
            return nullptr;
        return &(*it);
    }

    const SemanticBufferLayout* GraphicShader::getSemanticBuffer(details::CBufferUpdateType updateType) const noexcept
    {
        auto it = std::find_if(_semanticLayouts.begin(), _semanticLayouts.end(), [updateType](const auto& buffer) {return updateType == buffer.type; });
        if (it == _semanticLayouts.end())
            return nullptr;
        return &(*it);
    }

}
