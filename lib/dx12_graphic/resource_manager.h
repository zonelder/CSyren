#pragma once

#include "forward_decl.h"
#include "resource_handle.h"
#include "mesh.h"
#include "texture.h"
#include "material.h"
#include "shader.h"
#include "compute_shader.h"
#include "animation_clip.h"
#include "resource_upload_pipeline.h"
#include "texture_upload_task.h"
#include "renderer.h"

#include "cstdmf/page_view.h"
#include "cstdmf/log.h"



#include <string>
#include <vector>
#include <unordered_map>
#include <functional>
#include <type_traits>
#include <stdexcept>
#include <limits>

namespace csyren::render
{
    // Forward declarations
    class ResourceManager;

    // --- ResourceStorage: Manages a single type of resource ---
    template<class TResource>
    class ResourceStorage
    {
    public:

        using handle_type = THandle<TResource>;
        // Constructor takes core dependencies
        ResourceStorage(ResourceManager& resourceManager)
            :_resourceManager(resourceManager) {
        }

        ~ResourceStorage() = default;
        ResourceStorage(const ResourceStorage&) = delete;
        ResourceStorage& operator=(const ResourceStorage&) = delete;

        // Unified method for creating/loading a resource
        // TInitArgs will be passed to TResource::init
        template<typename... TInitArgs>
        handle_type load(const std::string& name, TInitArgs&&... init_args)
        {
            if (auto it = _nameToHandleMap.find(name); it != _nameToHandleMap.end())
            {
                return it->second;
            }

            auto [handle, resource_ptr] = create(name);
            if (!handle)return{};
            if (!resource_ptr->init(std::forward<TInitArgs>(init_args)...))
            {
                unload(name);
                log::error("Failed to initialize resource: {}", name);
                return {};
            }
            log::debug("Resource loaded/created: {}", name);
            return handle;
        }

        std::pair<handle_type, TResource*> create(const std::string& name)
        {
            if (const auto existing = find(name)) return {existing, get(existing)};
            // Never reset on unloadAll: cleared pages can reuse the same slot IDs.
            // Refuse to wrap so an old handle can never become current again.
            if (_lastGeneration == std::numeric_limits<uint64_t>::max())
                throw std::overflow_error("Resource generation exhausted");
            handle_type handle{ _storage.emplace(), ++_lastGeneration };

            TResource* resource_ptr = _storage.get(handle.id);
            _nameToHandleMap[name] = handle;
            _handleToNameMap[handle] = name;
            return { handle,resource_ptr };
        }

        // Basic accessors
        TResource* get(handle_type handle)
        {
            return _handleToNameMap.contains(handle) ? _storage.get(handle.id) : nullptr;
        }

        const TResource* get(handle_type handle) const
        {
            return _handleToNameMap.contains(handle) ? _storage.get(handle.id) : nullptr;
        }

        handle_type find(const std::string& name)
        {
            if (auto it = _nameToHandleMap.find(name); it != _nameToHandleMap.end())
            {
                return it->second;
            }
            return { handle_type::INVALID };
        }

        const std::string& getName(handle_type handle) const
        {
            if (auto it = _handleToNameMap.find(handle); it != _handleToNameMap.end())
            {
                return it->second;
            }
            static const std::string empty_string = "";
            return empty_string;
        }

        // Unloading
        std::shared_ptr<TResource> extract(handle_type handle)
        {
            auto* resource = get(handle);
            if (!resource) return {};
            auto retired = std::make_shared<TResource>(std::move(*resource));
            unload(handle);
            return retired;
        }

        void unload(handle_type handle)
        {
            if (_handleToNameMap.contains(handle))
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
        ResourceManager& _resourceManager;

        std::unordered_map<std::string, handle_type> _nameToHandleMap;
        std::unordered_map<handle_type, std::string> _handleToNameMap; // For reverse lookup
        cstdmf::PageView<TResource> _storage;
        uint64_t _lastGeneration = 0;
    };
    
    // --- ResourceManager: The central facade ---
    class ResourceManager
    {
    public:
        explicit ResourceManager() :
            _meshStorage(*this),
            _textureStorage(*this),
            _materialStorage(*this),
            _shaderStorage(*this),
            _computeShaderStorage(*this),
            _animationStorage(*this)
        {
        }

        ~ResourceManager();
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
            auto& storage = getStorage<TResource>();
            auto handle = storage.find(name);
            if (handle)
            {
                log::debug("Resource found in cache: {}", name);
                return handle;
            }

            auto& factoryMap = getFactoryMap<TResource>();
            if (auto it = factoryMap.find(name); it != factoryMap.end())
            {
                log::debug("Creating procedural resource: {}", name);
                return it->second(*this); // Factory will call create... and store it
            }

            if constexpr (std::is_same<TResource, Texture>::value)
            {
                log::debug("ResourceManager::get: async load from file: {} ", name);
                auto [new_handle, ptr] = storage.create(name);

                if (!new_handle) return {};
                try { enqueueTexture(new_handle, name); }
                catch (...) { storage.unload(new_handle); throw; }
                return new_handle;
            }
            else
            {
                log::debug("sync load resource from file: {}", name);
                return storage.load(name,name);
            }

        }

        // --- Explicit Create Methods (for in-memory/programmatic creation) ---
        // These methods directly call ResourceStorage::load with specific init_args.
        // They bypass the procedural factory lookup and file loading.

        // Meshes
        MeshHandle createMesh(const std::string& name,const MeshBuilder& builder)
        {
            return _meshStorage.load(name,builder);
        }

        // Textures
        // Example: create from raw pixel data (ImageData struct would be defined elsewhere)
        // TextureHandle createTexture(const std::string& name, const ImageData& data)
        // {
        //     return _textureStorage.load(name, data);
        // }

        // Shaders (from string code)
        ShaderHandle createShader(const std::string& name, const std::string& filepath);

        ShaderHandle createShaderFromCode(const std::string& name, const std::string& code);

        ComputeShaderHandle createComputeShader(const std::string& name, const std::string& filepath);
        ComputeShaderHandle createComputeShaderFromCode(const std::string& name, const std::string& code);
        bool saveShader(ShaderHandle handle, const std::string& filepath);
        bool saveComputeShader(ComputeShaderHandle handle, const std::string& filepath);

        // Materials
        MaterialHandle createMaterial(const std::string& name, ShaderHandle shader, const MaterialStateDesc& states);

        // --- Accessors for resource data (e.g., for rendering) ---
        Mesh* getMesh(MeshHandle handle) { return _meshStorage.get(handle); }
        Texture* getTexture(TextureHandle handle) { return _textureStorage.get(handle); }
        Material* getMaterial(MaterialHandle handle) { return _materialStorage.get(handle); }
        GraphicShader* getShader(ShaderHandle handle) { return _shaderStorage.get(handle); }
        ComputeShader* getComputeShader(ComputeShaderHandle handle) { return _computeShaderStorage.get(handle); }
        AnimationClip* getAnimationClip(AnimationClipHandle handle) { return _animationStorage.get(handle); }

        const Mesh* getMesh(MeshHandle handle) const { return _meshStorage.get(handle); }
        const Texture* getTexture(TextureHandle handle) const { return _textureStorage.get(handle); }
        const Material* getMaterial(MaterialHandle handle) const { return _materialStorage.get(handle); }
        const GraphicShader* getShader(ShaderHandle handle) const { return _shaderStorage.get(handle); }
        const ComputeShader* getComputeShader(ComputeShaderHandle handle) const { return _computeShaderStorage.get(handle); }
        const AnimationClip* getAnimationClip(AnimationClipHandle handle) const { return _animationStorage.get(handle); }

        LoadStatus status(TextureHandle handle) const {
            const auto* texture = getTexture(handle);
            return texture ? texture->status() : LoadStatus::Failed;
        }
        TextureHandle reloadTexture(const std::string& name) {
            unload<Texture>(name);
            return get<Texture>(name);
        }

        // --- Get resource name by handle ---
        const std::string& getMeshName(MeshHandle handle) const { return _meshStorage.getName(handle); }
        const std::string& getTextureName(TextureHandle handle) const { return _textureStorage.getName(handle); }
        const std::string& getMaterialName(MaterialHandle handle) const { return _materialStorage.getName(handle); }
        const std::string& getShaderName(ShaderHandle handle) const { return _shaderStorage.getName(handle); }
        const std::string& getComputeShaderName(ComputeShaderHandle handle) const { return _computeShaderStorage.getName(handle); }
        const std::string& getAnimationClipName(AnimationClipHandle handle) const { return _animationStorage.getName(handle); }

        // --- Unloading Resources ---
        template<typename TResource>
        void unload(const std::string& name)
        {
            unload(getStorage<TResource>().find(name));
        }

        template<typename TResource>
        void unload(THandle<TResource> handle)
        {
            if (auto resource = getStorage<TResource>().extract(handle)) retire(std::move(resource));
        }

        // Owner-thread API. update() runs once at frame acquire, before recording.
        void init();

        void shutdown();

        void update();
    private:
        void enqueueTexture(TextureHandle handle, const std::string& name);
        void retire(std::shared_ptr<void> resource);
        std::unique_ptr<ResourceUploadPipeline> _uploads;
        // Specialized ResourceStorage instances
        ResourceStorage<Mesh> _meshStorage;
        ResourceStorage<Texture> _textureStorage;
        ResourceStorage<Material> _materialStorage;
        ResourceStorage<GraphicShader> _shaderStorage;
        ResourceStorage<ComputeShader> _computeShaderStorage;
        ResourceStorage<AnimationClip> _animationStorage;

        // Maps for procedural resource factories
        std::unordered_map<std::string, ProceduralResourceFactory<Mesh>> _proceduralMeshFactories;
        std::unordered_map<std::string, ProceduralResourceFactory<Texture>> _proceduralTextureFactories;
        std::unordered_map<std::string, ProceduralResourceFactory<Material>> _proceduralMaterialFactories;
        std::unordered_map<std::string, ProceduralResourceFactory<GraphicShader>> _proceduralShaderFactories;
        std::unordered_map<std::string, ProceduralResourceFactory<ComputeShader>> _proceduralComputeShaderFactories;
        std::unordered_map<std::string, ProceduralResourceFactory<AnimationClip>> _proceduralAnimationFactories;

        // Helper to get the correct storage based on type
        template<typename T> ResourceStorage<T>& getStorage();
        template<> ResourceStorage<Mesh>& getStorage<Mesh>() { return _meshStorage; }
        template<> ResourceStorage<Texture>& getStorage<Texture>() { return _textureStorage; }
        template<> ResourceStorage<Material>& getStorage<Material>() { return _materialStorage; }
        template<> ResourceStorage<GraphicShader>& getStorage<GraphicShader>() { return _shaderStorage; }
        template<> ResourceStorage<ComputeShader>& getStorage<ComputeShader>() { return _computeShaderStorage; }
        template<> ResourceStorage<AnimationClip>& getStorage<AnimationClip>() { return _animationStorage; }

        // Helper to get the correct factory map based on type
        template<typename T> std::unordered_map<std::string, ProceduralResourceFactory<T>>& getFactoryMap();
        template<> std::unordered_map<std::string, ProceduralResourceFactory<Mesh>>& getFactoryMap<Mesh>() { return _proceduralMeshFactories; }
        template<> std::unordered_map<std::string, ProceduralResourceFactory<Texture>>& getFactoryMap<Texture>() { return _proceduralTextureFactories; }
        template<> std::unordered_map<std::string, ProceduralResourceFactory<Material>>& getFactoryMap<Material>() { return _proceduralMaterialFactories; }
        template<> std::unordered_map<std::string, ProceduralResourceFactory<GraphicShader>>& getFactoryMap<GraphicShader>() { return _proceduralShaderFactories; }
        template<> std::unordered_map<std::string, ProceduralResourceFactory<ComputeShader>>& getFactoryMap<ComputeShader>() { return _proceduralComputeShaderFactories; }
        template<> std::unordered_map<std::string, ProceduralResourceFactory<AnimationClip>>& getFactoryMap<AnimationClip>() { return _proceduralAnimationFactories; }
    };
}
