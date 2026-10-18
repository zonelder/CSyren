#pragma once
#include "forward_decl.h"
#include <limits>
#include <cstdint>
#include <functional>
#include <tuple>

namespace csyren::render
{
    template<typename T>
    struct THandle
    {
        static constexpr uint64_t INVALID = { std::numeric_limits< uint64_t>::max() };
        uint64_t id{ INVALID };
        // Runtime identity of this allocation; zero means no generation was issued.
        uint64_t generation{};

        bool operator<(const THandle<T>& other) const { return std::tie(id, generation) < std::tie(other.id, other.generation); }
        bool operator==(const THandle<T>& other) const = default;

        explicit operator bool() const { return id != INVALID && generation != 0; }
    };

    using MeshHandle = THandle<Mesh>;
    using TextureHandle = THandle<Texture>;
    using MaterialHandle = THandle<Material>;
    using ShaderHandle = THandle<GraphicShader>;
    using ComputeShaderHandle = THandle<ComputeShader>;

}

namespace std 
{
    template<typename T>
    struct hash<csyren::render::THandle<T>>
    {
        std::size_t operator()(const csyren::render::THandle<T>& handle) const
        {
            return std::hash<uint64_t>()(handle.id) ^ (std::hash<uint64_t>()(handle.generation) << 1);
        }
    };
}
