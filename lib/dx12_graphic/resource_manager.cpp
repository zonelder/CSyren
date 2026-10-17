#include "resource_manager.h"
#include "resource_manager.h"
#include "pch.h"
#include "resource_manager.h"

namespace csyren::render
{

    void ResourceManager::init()
    {
        if (!_pUploadThread)
            _pUploadThread = std::make_unique<ResourceUploadThread>();
    }
    // Shaders (from string code)
    ShaderHandle ResourceManager::createShader(const std::string& name, const std::string& filepath)
    {
        return _shaderStorage.load(name, filepath);
    }

    ShaderHandle ResourceManager::createShaderFromCode(const std::string& name, const std::string& code)
    {
        return _shaderStorage.load(name, from_source_code, code);
    }

    // Materials
    ComputeShaderHandle ResourceManager::createComputeShader(const std::string& name, const std::string& filepath)
    {
        return _computeShaderStorage.load(name, filepath);
    }

    ComputeShaderHandle ResourceManager::createComputeShaderFromCode(const std::string& name, const std::string& code)
    {
        return _computeShaderStorage.load(name, from_source_code, code);
    }

    bool ResourceManager::saveShader(ShaderHandle handle, const std::string& filepath)
    {
        const auto* shader = getShader(handle);
        return shader && shader->save(filepath);
    }

    bool ResourceManager::saveComputeShader(ComputeShaderHandle handle, const std::string& filepath)
    {
        const auto* shader = getComputeShader(handle);
        return shader && shader->save(filepath);
    }

    MaterialHandle ResourceManager::createMaterial(const std::string& name, ShaderHandle shader, const MaterialStateDesc& states)
    {
        return _materialStorage.load(name, shader, states);
    }
    void csyren::render::ResourceManager::shutdown()
    {
        _proceduralMeshFactories.clear();
        _proceduralTextureFactories.clear();
        _proceduralShaderFactories.clear();
        _proceduralComputeShaderFactories.clear();
        _proceduralMaterialFactories.clear();
        _meshStorage.unloadAll();
        _textureStorage.unloadAll();
        _materialStorage.unloadAll();
        _shaderStorage.unloadAll();
        _computeShaderStorage.unloadAll();
        log::debug("All resources unloaded.");
    }
}


