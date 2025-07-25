#ifndef __CSYREN_RESOURCE_MANAGER__
#define __CSYREN_RESOURCE_MANAGER__

#include <cstdint>
#include <unordered_map>
#include <map>
#include <numeric>

#include "cstdmf/page_view.h"
#include "renderer.h"
#include "mesh.h"
#include "Texture.h"
#include "material.h"

namespace csyren::render
{
    class Renderer;

    template<typename T>
    struct THandle
    {
        static constexpr uint64_t INVALID = { std::numeric_limits< uint64_t>::max()};
        uint64_t id;

        bool operator<(const THandle<T>& other) const { return id < other.id; }
        bool operator==(const THandle<T>& other) const { return id == other.id; }

        explicit operator bool() const { return id != INVALID; }
    };

    using MeshHandle = THandle<Mesh>;
    using TextureHandle = THandle<Texture>;
    using MaterialHandle = THandle<Material>;

    template<class TResource>
    class ResourceStorage
    {
    public:
        ResourceStorage() = default;
        ~ResourceStorage() = default;

        ResourceStorage(const ResourceStorage&) = delete;
        ResourceStorage& operator=(const ResourceStorage&) = delete;
        template<typename... TInitArgs>
        THandle<TResource> load(const std::string& name, TInitArgs&&... init_args)
        {
            if (auto it = _nameToHandleMap.find(name); it != _nameToHandleMap.end())
            {
                return it->second;
            }

            THandle<TResource> handle{ _storage.emplace() };

            if (!handle)
            {
                return {};
            }

            TResource* resource_ptr = _storage.get(handle.id);

            if (!resource_ptr->init(std::forward<TInitArgs>(init_args)...))
            {
                _storage.erase(handle.id);
                return {};
            }

            _nameToHandleMap[name] = handle;
            return handle;
        }

        void unload(THandle<TResource> handle)
        {
            if (!handle) return;

            _storage.erase(handle.id);
            for (auto it = _nameToHandleMap.begin(); it != _nameToHandleMap.end(); ++it)
            {
                if (it->second == handle)
                {
                    _nameToHandleMap.erase(it);
                    break;
                }
            }
        }

        TResource* get(const std::string& name)
        {
            auto handle = find(name);
            return handle ? _storage.get(handle.id) : nullptr;
        }

        const TResource* get(const std::string& name) const
        {
            auto handle = find(name);
            return handle ? _storage.get(handle.id) : nullptr;
        }

        TResource* get(THandle<TResource> handle)
        {
            return _storage.get(handle.id);
        }

        const TResource* get(THandle<TResource> handle) const
        {
            return _storage.get(handle.id);
        }

        THandle<TResource> find(const std::string& name) const
        {
            if (auto it = _nameToHandleMap.find(name); it != _nameToHandleMap.end())
            {
                return it->second;
            }
            return {}; // Return invalid handle
        }




    private:
        std::unordered_map<std::string, THandle<TResource>> _nameToHandleMap;
        cstdmf::PageView<TResource> _storage;
    };

	class ResourceManager
	{
    public:
        explicit ResourceManager(csyren::render::Renderer& renderer)
            : _renderer(renderer)
        {
        }

        ~ResourceManager() = default;
        ResourceManager(const ResourceManager&) = delete;
        ResourceManager& operator=(const ResourceManager&) = delete;

        template<typename... TArgs>
        MeshHandle loadMesh(const std::string& name, TArgs&&... args)
        {
            return _meshStorage.load(name, _renderer, std::forward<TArgs>(args)...);
        }
        Mesh* getMesh(MeshHandle handle) { return _meshStorage.get(handle); }

        template<typename... TArgs>
        TextureHandle loadTexture(const std::string& name, TArgs&&... args)
        {
            return _textureStorage.load(name, _renderer, std::forward<TArgs>(args)...);
        }
        Texture* getTexture(TextureHandle handle) { return _textureStorage.get(handle); }

        template<typename... TArgs>
        MaterialHandle loadMaterial(const std::string& name, TArgs&&... args)
        {
            return _materialStorage.load(name, _renderer, std::forward<TArgs>(args)...);
        }
        Material* getMaterial(MaterialHandle handle) { return _materialStorage.get(handle); }

    private:
        csyren::render::Renderer& _renderer;

        // A dedicated storage object for each resource type.
        ResourceStorage<csyren::render::Mesh>     _meshStorage;
        ResourceStorage<csyren::render::Texture>  _textureStorage;
        ResourceStorage<csyren::render::Material> _materialStorage;
	};
}

#endif