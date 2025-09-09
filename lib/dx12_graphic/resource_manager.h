#pragma once

#include <string>
#include <vector>
#include <unordered_map>
#include <functional>


#include "cstdmf/page_view.h"
#include "cstdmf/log.h"

#include "resource_handle.h"
#include "renderer.h"
#include "mesh.h"
#include "texture.h"
#include "material.h"
#include "shader.h"

namespace csyren::render
{
    // Forward declarations
    class ResourceManager;

    // --- ResourceStorage: Manages a single type of resource ---
    template<class TResource>
    class ResourceStorage
    {
    public:
        // Constructor takes core dependencies
        ResourceStorage(Renderer& renderer, ResourceManager& resourceManager)
            : _renderer(renderer), _resourceManager(resourceManager) {
        }

        ~ResourceStorage() = default;
        ResourceStorage(const ResourceStorage&) = delete;
        ResourceStorage& operator=(const ResourceStorage&) = delete;

        // Unified method for creating/loading a resource
        // TInitArgs will be passed to TResource::init
        template<typename... TInitArgs>
        THandle<TResource> load(const std::string& name, TInitArgs&&... init_args)
        {
            if (auto it = _nameToHandleMap.find(name); it != _nameToHandleMap.end())
            {
                return it->second;
            }

            THandle<TResource> handle{ _storage.emplace() };
            if (!handle) return {};

            TResource* resource_ptr = _storage.get(handle.id);

            // Call the resource's init method, passing dependencies and specific init args
            if (!resource_ptr->init(_renderer, _resourceManager, std::forward<TInitArgs>(init_args)...))
            {
                _storage.erase(handle.id);
                log::error("Failed to initialize resource: {}", name);
                return {};
            }

            _nameToHandleMap[name] = handle;
            _handleToNameMap[handle] = name;
            log::debug("Resource loaded/created: {}", name);
            return handle;
        }

        // Basic accessors
        TResource* get(THandle<TResource> handle)
        {
            return _storage.get(handle.id);
        }

        THandle<TResource> find(const std::string& name)
        {
            if (auto it = _nameToHandleMap.find(name); it != _nameToHandleMap.end())
            {
                return it->second;
            }
            return { THandle<TResource> ::INVALID };
        }

        const std::string& getName(THandle<TResource> handle) const
        {
            if (auto it = _handleToNameMap.find(handle); it != _handleToNameMap.end())
            {
                return it->second;
            }
            static const std::string empty_string = "";
            return empty_string;
        }

        // Unloading
        void unload(THandle<TResource> handle)
        {
            if (_storage.contains(handle.id))
            {
                std::string name = getName(handle);
                _storage.erase(handle.id);
                _nameToHandleMap.erase(name);
                _handleToNameMap.erase(handle);
                log::debug("Resource unloaded: {}", name);
            }
        }

        void unload(const std::string& name)
        {
            if (auto handle = find(name))
            {
                unload(handle);
            }
        }

        void unloadAll()
        {
            _storage.clear();
            _nameToHandleMap.clear();
            _handleToNameMap.clear();
            log::debug("All resources of type {} unloaded.", typeid(TResource).name());
        }

    private:
        Renderer& _renderer;
        ResourceManager& _resourceManager;

        std::unordered_map<std::string, THandle<TResource>> _nameToHandleMap;
        std::unordered_map<THandle<TResource>, std::string> _handleToNameMap; // For reverse lookup
        cstdmf::PageView<TResource> _storage;
    };

    // --- ResourceManager: The central facade ---
    class ResourceManager
    {
    public:
        explicit ResourceManager(Renderer& renderer)
            : _renderer(renderer),
            _meshStorage(renderer, *this),
            _textureStorage(renderer, *this),
            _materialStorage(renderer, *this),
            _shaderStorage(renderer, *this)
        {
        }

        ~ResourceManager() = default;
        ResourceManager(const ResourceManager&) = delete;
        ResourceManager& operator=(const ResourceManager&) = delete;

        // --- Procedural Resource Factory Type ---
        template<typename TResource>
        using ProceduralResourceFactory = std::function<THandle<TResource>(ResourceManager&)>;

        // --- Registration for Procedural Resources ---
        template<typename TResource>
        void registerProcedural(const std::string& name, ProceduralResourceFactory<TResource> factory)
        {
            getFactoryMap<TResource>()[name] = factory;
            log::debug("Procedural resource factory registered: {}", name);
        }

        // --- Unified Get Method (main entry point for all resources) ---
        template<typename TResource>
        THandle<TResource> get(const std::string& name)
        {
            // 1. Check if already in cache
            auto& storage = getStorage<TResource>();
            auto handle = storage.find(name);
            if (handle.id != THandle<TResource>::INVALID)
            {
                log::debug("Resource found in cache: {}", name);
                return handle;
            }

            // 2. Check if a procedural factory is registered for this name
            auto& factoryMap = getFactoryMap<TResource>();
            if (auto it = factoryMap.find(name); it != factoryMap.end())
            {
                log::debug("Creating procedural resource: {}", name);
                return it->second(*this); // Factory will call create... and store it
            }

            // 3. Assume it's a file path and try to load from disk
            log::debug("Loading resource from file: {}", name);
            // This assumes TResource::init has an overload that takes a single string (filepath)
            return storage.load(name, name);
        }

        // --- Explicit Create Methods (for in-memory/programmatic creation) ---
        // These methods directly call ResourceStorage::load with specific init_args.
        // They bypass the procedural factory lookup and file loading.

        // Meshes
        MeshHandle createMesh(const std::string& name, const std::vector<Mesh::Vertex>& vertices, const std::vector<uint16_t>& indices)
        {
            return _meshStorage.load(name, vertices, indices);
        }

        // Textures
        // Example: create from raw pixel data (ImageData struct would be defined elsewhere)
        // TextureHandle createTexture(const std::string& name, const ImageData& data)
        // {
        //     return _textureStorage.load(name, data);
        // }

        // Shaders (from string code)
        ShaderHandle createShader(const std::string& name, const std::string& vsCode, const std::string& psCode)
        {
            return _shaderStorage.load(name, vsCode, psCode);
        }

        // Materials
        MaterialHandle createMaterial(const std::string& name, ShaderHandle shader, const MaterialStateDesc& states)
        {
            return _materialStorage.load(name, shader, states);
        }

        // --- Accessors for resource data (e.g., for rendering) ---
        Mesh* getMesh(MeshHandle handle) { return _meshStorage.get(handle); }
        Texture* getTexture(TextureHandle handle) { return _textureStorage.get(handle); }
        Material* getMaterial(MaterialHandle handle) { return _materialStorage.get(handle); }
        Shader* getShader(ShaderHandle handle) { return _shaderStorage.get(handle); }

        // --- Get resource name by handle ---
        const std::string& getMeshName(MeshHandle handle) const { return _meshStorage.getName(handle); }
        const std::string& getTextureName(TextureHandle handle) const { return _textureStorage.getName(handle); }
        const std::string& getMaterialName(MaterialHandle handle) const { return _materialStorage.getName(handle); }
        const std::string& getShaderName(ShaderHandle handle) const { return _shaderStorage.getName(handle); }

        // --- Unloading Resources ---
        template<typename TResource>
        void unload(const std::string& name)
        {
            getStorage<TResource>().unload(name);
        }

        template<typename TResource>
        void unload(THandle<TResource> handle)
        {
            getStorage<TResource>().unload(handle);
        }

        void unloadAll()
        {
            _meshStorage.unloadAll();
            _textureStorage.unloadAll();
            _materialStorage.unloadAll();
            _shaderStorage.unloadAll();
            log::debug("All resources unloaded.");
        }

    private:
        Renderer& _renderer;

        // Specialized ResourceStorage instances
        ResourceStorage<Mesh> _meshStorage;
        ResourceStorage<Texture> _textureStorage;
        ResourceStorage<Material> _materialStorage;
        ResourceStorage<Shader> _shaderStorage;

        // Maps for procedural resource factories
        std::unordered_map<std::string, ProceduralResourceFactory<Mesh>> _proceduralMeshFactories;
        std::unordered_map<std::string, ProceduralResourceFactory<Texture>> _proceduralTextureFactories;
        std::unordered_map<std::string, ProceduralResourceFactory<Material>> _proceduralMaterialFactories;
        std::unordered_map<std::string, ProceduralResourceFactory<Shader>> _proceduralShaderFactories;

        // Helper to get the correct storage based on type
        template<typename T> ResourceStorage<T>& getStorage();
        template<> ResourceStorage<Mesh>& getStorage<Mesh>() { return _meshStorage; }
        template<> ResourceStorage<Texture>& getStorage<Texture>() { return _textureStorage; }
        template<> ResourceStorage<Material>& getStorage<Material>() { return _materialStorage; }
        template<> ResourceStorage<Shader>& getStorage<Shader>() { return _shaderStorage; }

        // Helper to get the correct factory map based on type
        template<typename T> std::unordered_map<std::string, ProceduralResourceFactory<T>>& getFactoryMap();
        template<> std::unordered_map<std::string, ProceduralResourceFactory<Mesh>>& getFactoryMap<Mesh>() { return _proceduralMeshFactories; }
        template<> std::unordered_map<std::string, ProceduralResourceFactory<Texture>>& getFactoryMap<Texture>() { return _proceduralTextureFactories; }
        template<> std::unordered_map<std::string, ProceduralResourceFactory<Material>>& getFactoryMap<Material>() { return _proceduralMaterialFactories; }
        template<> std::unordered_map<std::string, ProceduralResourceFactory<Shader>>& getFactoryMap<Shader>() { return _proceduralShaderFactories; }
    };
}