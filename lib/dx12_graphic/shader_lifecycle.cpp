#include "pch.h"
#include "shader_base.h"
#include "shader_file.h"

#include <d3dcompiler.h>
#include <cstring>

namespace csyren::render
{
    namespace
    {
        std::filesystem::path shaderCachePath(const std::filesystem::path& source)
        {
            auto key = source.generic_string();
            std::replace(key.begin(), key.end(), ':', '_');
            while (!key.empty() && key.front() == '/') key.erase(key.begin());
            return std::filesystem::path("cache:/") / key;
        }
    }
    std::span<const ShaderBase::StageDefinition> ShaderBase::stageDefinitions() const noexcept
    {
        static constexpr StageDefinition graphics[] = {
            { ShaderStage::Vertex, "vs_5_1", "VSMain", "_vs.cso", D3D12_SHADER_VISIBILITY_VERTEX, false },
            { ShaderStage::Pixel, "ps_5_1", "PSMain", "_ps.cso", D3D12_SHADER_VISIBILITY_PIXEL, false },
            { ShaderStage::Geometry, "gs_5_1", "GSMain", "_gs.cso", D3D12_SHADER_VISIBILITY_GEOMETRY, true },
            { ShaderStage::Hull, "hs_5_1", "HSMain", "_hs.cso", D3D12_SHADER_VISIBILITY_HULL, true },
            { ShaderStage::Domain, "ds_5_1", "DSMain", "_ds.cso", D3D12_SHADER_VISIBILITY_DOMAIN, true }
        };
        static constexpr StageDefinition compute[] = {
            { ShaderStage::Compute, "cs_5_1", "CSMain", "_cs.cso", D3D12_SHADER_VISIBILITY_ALL, false }
        };
        if (_type == ShaderType::Compute)
            return compute;
        return graphics;
    }

    const char* ShaderBase::sourceExtension() const noexcept
    {
        return _type == ShaderType::Compute ? ".coshader" : ".shader";
    }

    ID3DBlob* ShaderBase::stageBlob(ShaderStage stage) const noexcept
    {
        const auto index = static_cast<size_t>(stage);
        return index < _stageBlobs.size() ? _stageBlobs[index].Get() : nullptr;
    }

    D3D12_SHADER_BYTECODE ShaderBase::getBytecode(ShaderStage stage) const noexcept
    {
        auto* blob = stageBlob(stage);
        if (!blob)
            return { nullptr, 0 };
        return { blob->GetBufferPointer(), blob->GetBufferSize() };
    }

    bool ShaderBase::init(from_source_code_t tag, const std::string& code)
    {
        if (!core::Services::has<Renderer>())
            return false;
        auto* renderer = core::Services::get<Renderer>();
        return renderer && init(*renderer, tag, code);
    }

    bool ShaderBase::init(from_asset_path_t, const std::string& filepath)
    {
        return init(filepath);
    }

    bool ShaderBase::init(const std::string& filepath)
    {
        if (!core::Services::has<Renderer>())
            return false;
        auto* renderer = core::Services::get<Renderer>();
        return renderer && init(*renderer, filepath);
    }

    bool ShaderBase::init(Renderer& renderer, from_source_code_t tag, const std::string& code)
    {
        return init(renderer.device(), tag, code);
    }

    bool ShaderBase::init(Renderer& renderer, from_asset_path_t, const std::string& filepath)
    {
        return init(renderer.device(), filepath);
    }

    bool ShaderBase::init(Renderer& renderer, const std::string& filepath)
    {
        return init(renderer.device(), filepath);
    }

    bool ShaderBase::init(ID3D12Device* device, from_source_code_t, const std::string& code)
    {
        return device && compileAndInit(device, code);
    }

    bool ShaderBase::init(ID3D12Device* device, from_asset_path_t, const std::string& filepath)
    {
        return init(device, filepath);
    }

    bool ShaderBase::init(ID3D12Device* device, const std::string& filepath)
    {
        if (!device || filepath.empty())
            return false;
        const std::filesystem::path path(filepath);
        if (path.extension() == sourceExtension())
        {
            std::string source;
            if (!details::loadShaderSource(filepath, sourceExtension(), source))
            {
                log::error("ShaderBase::init: Could not read source: {}", filepath);
                return false;
            }
            return compileAndInit(device, source);
        }
        if (path.extension() == ".shader" || path.extension() == ".coshader" || _type == ShaderType::Compute)
        {
            log::error("ShaderBase::init: Expected a {} file: {}", sourceExtension(), filepath);
            return false;
        }

        // Preserve the original graphics source/cache workflow for legacy assets.
#if defined(_DEBUG)
        std::string source;
        const auto extension = path.extension().string();
        if (!details::loadShaderSource(filepath, extension.c_str(), source) || !compileAndInit(device, source))
            return false;
        return savePrecompiled(path);
#else
        return loadPrecompiledAndInit(device, path);
#endif
    }

    bool ShaderBase::save(const std::string& filepath) const
    {
        if (details::saveShaderSource(filepath, sourceExtension(), _sourceCode))
            return true;
        log::error("ShaderBase::save: Could not save source: {}", filepath);
        return false;
    }

    bool ShaderBase::compileAndInit(ID3D12Device* device, const std::string& code)
    {
        decltype(_stageBlobs) stages;
        for (const auto& definition : stageDefinitions())
        {
            auto compiled = compileStage(code, definition.target, definition.entryPoint, definition.optional);
            if (!compiled.success)
                return false;
            stages[static_cast<size_t>(definition.stage)] = std::move(compiled.blob);
        }
        auto meta = ShaderMetaBuilder::build(code);
        if (!meta)
            return false;
        _stageBlobs = std::move(stages);
        _meta = std::move(meta);
        _sourceCode.clear();
        if (!finishInit(device))
            return false;
        _sourceCode = code;
        return true;
    }

    bool ShaderBase::finishInit(ID3D12Device* device)
    {
        std::vector<std::pair<D3D12_SHADER_VISIBILITY, ID3DBlob*>> blobs;
        for (const auto& definition : stageDefinitions())
        {
            if (auto* blob = stageBlob(definition.stage))
                blobs.emplace_back(definition.visibility, blob);
        }
        return buildRootSignatureFromReflection(device, blobs) && finalizeInit(device);
    }

    bool ShaderBase::savePrecompiled(const std::filesystem::path& relativePath) const
    {
        const auto buildPath = shaderCachePath(relativePath);
        auto& files = cstdmf::FileSystem::instance();
        auto metaPath = buildPath;
        metaPath.replace_extension("json");
        if (!ShaderMetaBuilder::save(*_meta, metaPath.string()))
            return false;
        for (const auto& definition : stageDefinitions())
        {
            auto blobPath = buildPath;
            blobPath.replace_extension(definition.cacheSuffix);
            if (auto* blob = stageBlob(definition.stage))
            {
                const auto bytes = std::span(static_cast<const std::byte*>(blob->GetBufferPointer()), blob->GetBufferSize());
                if (!files.writeFile(blobPath, bytes))
                    return false;
            }
            else
            {
                // Avoid resurrecting an optional stage removed from the source.
                if (!files.remove(blobPath))
                    return false;
            }
        }
        return true;
    }

    bool ShaderBase::loadPrecompiledAndInit(ID3D12Device* device, const std::filesystem::path& relativePath)
    {
        const auto buildPath = shaderCachePath(relativePath);
        auto& files = cstdmf::FileSystem::instance();
        auto metaPath = buildPath;
        metaPath.replace_extension("json");
        std::string metadata;
        if (!files.readFile(metaPath, metadata))
            return false;
        ShaderMetaPtr meta;
        try
        {
            meta = ShaderMetaBuilder::build(nlohmann::json::parse(metadata));
        }
        catch (const nlohmann::json::exception& error)
        {
            log::error("ShaderBase::init: Invalid metadata {}: {}", metaPath.string(), error.what());
            return false;
        }
        if (!meta)
            return false;
        decltype(_stageBlobs) stages;
        for (const auto& definition : stageDefinitions())
        {
            auto blobPath = buildPath;
            blobPath.replace_extension(definition.cacheSuffix);
            if (definition.optional && !files.exists(blobPath))
                continue;
            std::vector<std::byte> bytes;
            if (!files.readFile(blobPath, bytes) || bytes.empty())
                return false;
            auto& blob = stages[static_cast<size_t>(definition.stage)];
            if (FAILED(D3DCreateBlob(bytes.size(), &blob))) return false;
            std::memcpy(blob->GetBufferPointer(), bytes.data(), bytes.size());
        }
        _stageBlobs = std::move(stages);
        _meta = std::move(meta);
        _sourceCode.clear();
        return finishInit(device);
    }
}
