#pragma once
#include <limits>

namespace csyren::render
{
    class Mesh;
    class Texture;
    class Material;
    class Shader;

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
    using ShaderHandle = THandle<Shader>;

}