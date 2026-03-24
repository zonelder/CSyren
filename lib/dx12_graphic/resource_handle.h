#pragma once
#include "forward_decl.h"
#include <limits>

namespace csyren::render
{
    template<typename T>
    struct THandle
    {
        static constexpr uint64_t INVALID = { std::numeric_limits< uint64_t>::max() };
        uint64_t id;

        bool operator<(const THandle<T>& other) const { return id < other.id; }
        bool operator==(const THandle<T>& other) const { return id == other.id; }

        explicit operator bool() const { return id != INVALID; }
    };

    using MeshHandle = THandle<Mesh>;
    using TextureHandle = THandle<Texture>;
    using MaterialHandle = THandle<Material>;
    using ShaderHandle = THandle<GraphicShader>;

}

namespace std {
    template<typename T>
    struct hash<csyren::render::THandle<T>>
    {
        std::size_t operator()(const csyren::render::THandle<T>& handle) const
        {
            return std::hash<uint64_t>()(handle.id);
        }
    };
}