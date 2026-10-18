#include "pch.h"
#include "resource_manager.h"
#include "core/system_manager.h"

namespace csyren::render
{

    void ResourceManager::init()
    {
        if (!_uploads)
            _uploads = std::make_unique<ResourceUploadPipeline>(core::Services::get<core::SystemManager>()->jobs());
    }
    ResourceManager::~ResourceManager() { shutdown(); }

    void ResourceManager::enqueueTexture(TextureHandle handle, const std::string& name)
    {
        if (!_uploads) throw std::logic_error("Initialize ResourceManager before requesting file textures");
        _uploads->addTask(std::make_unique<TextureUploadTask>(handle, name));
    }

    void ResourceManager::retire(std::shared_ptr<void> resource)
    {
        if (core::Services::has<Renderer>() && core::Services::get<Renderer>()->device())
            core::Services::get<Renderer>()->deferRelease(std::move(resource));
    }

    void ResourceManager::update()
    { if (_uploads) _uploads->update(*this); }
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
        // Called between frames while Renderer and DescriptorManager still live.
        if (_uploads) { _uploads->shutdown(); _uploads.reset(); }
        if (core::Services::has<Renderer>() && core::Services::get<Renderer>()->device())
            core::Services::get<Renderer>()->flushDeferredReleases();
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


