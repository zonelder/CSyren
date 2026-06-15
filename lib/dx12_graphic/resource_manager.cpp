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
    MaterialHandle ResourceManager::createMaterial(const std::string& name, ShaderHandle shader, const MaterialStateDesc& states)
    {
        return _materialStorage.load(name, shader, states);
    }
    void csyren::render::ResourceManager::shutdown()
    {
        _proceduralMeshFactories.clear();
        _proceduralTextureFactories.clear();
        _proceduralShaderFactories.clear();
        _proceduralMaterialFactories.clear();
        _meshStorage.unloadAll();
        _textureStorage.unloadAll();
        _materialStorage.unloadAll();
        _shaderStorage.unloadAll();
        log::debug("All resources unloaded.");
    }
}


