#include "pch.h"
#include "primitives.h"
#include "mesh.h"
#include "material.h"
#include "renderer.h"
#include "vertex_formats.h"

#include <d3dcompiler.h>
#include <vector>

namespace csyren::render
{
    namespace
    {
        constexpr char TEXTURE_SHADER_NAME[]        = "__csyren_texture_shader__";
        constexpr char TEXTURE_MATERIAL_NAME[]      = "__primitive_texture_material__";

        constexpr char DEFAULT_SHADER_NAME[]        = "__csyren_default_shader__";
        constexpr char DEFAULT_MATERIAL_NAME[]      = "__primitive_default_material__";

        constexpr char RAINBOW_SHADER_NAME[]        = "__csyren_rainbow_shader__";
        constexpr char RAINBOW_MATERIAL_NAME[]      = "__primitive_rainbow_material__";
        constexpr char LINE_MESH_NAME[]             = "__primitive_line__";
        constexpr char TRIANGLE_MESH_NAME[]         ="__primitive_triangle__";
        constexpr char QUAD_MESH_NAME[]             ="__primitive_quad__";
        constexpr char CUBE_MESH_NAME[]             ="__primitive_cube__";
        constexpr char SPHERE_MESH_NAME[]           = "__primitive_sphere__";
    }

    bool Primitives::registerFabricsAll()
    {
        auto& rm =  *core::Services::get<ResourceManager>();

        ResourceManager::ProceduralResourceFactory<GraphicShader> defaultShaderFabric = [](ResourceManager& rm) { return rm.createShader(DEFAULT_SHADER_NAME, "shaders/default.shader"); };

        ResourceManager::ProceduralResourceFactory<Material> defaultMaterialFabric = [](ResourceManager& rm)
            {
                auto shader = rm.get<GraphicShader>(DEFAULT_SHADER_NAME);
                MaterialStateDesc defaultStates = {};
                const auto handle = rm.createMaterial(DEFAULT_MATERIAL_NAME, shader, defaultStates);
                if (auto* material = rm.getMaterial(handle)) initializeMaterialDefaults(*material);
                return handle;
            };
        ResourceManager::ProceduralResourceFactory<Material> textureMaterialFabric = [](ResourceManager& rm)
            {
                auto shader = rm.get<GraphicShader>(TEXTURE_SHADER_NAME);
                MaterialStateDesc defaultStates = {};
                return rm.createMaterial(TEXTURE_MATERIAL_NAME, shader, defaultStates);
            };

        ResourceManager::ProceduralResourceFactory<Mesh> lineMeshFabric = [](ResourceManager& rm) {
            MeshBuilder builder;
            builder
                .addVertex({ 0,0,0 }).addColor({ 1,1,1,1 })
                .addVertex({ 1,0,0 }).addColor({ 1,1,1,1 })
                .addIndex(0).addIndex(1);
            return rm.createMesh(LINE_MESH_NAME, builder);
            };

        ResourceManager::ProceduralResourceFactory<GraphicShader> rainbowShaderFabric =
            [](ResourceManager& rm) { return rm.createShader(RAINBOW_SHADER_NAME, "shaders/rainbow.shader"); };


        ResourceManager::ProceduralResourceFactory<GraphicShader> textureShaderFabric =
            [](ResourceManager& rm) { return rm.createShader(TEXTURE_SHADER_NAME, "shaders/texture.shader"); };


        ResourceManager::ProceduralResourceFactory<Material> rainbowMaterialFabric =
            [](ResourceManager& rm)
            {
                auto shader = rm.get<GraphicShader>(RAINBOW_SHADER_NAME);
                MaterialStateDesc states = {};
                const auto handle = rm.createMaterial(RAINBOW_MATERIAL_NAME, shader, states);
                if (auto* material = rm.getMaterial(handle)) initializeMaterialDefaults(*material);
                return handle;
            };

        ResourceManager::ProceduralResourceFactory<Mesh> triangleMeshFabric = [](ResourceManager& rm) {
            MeshBuilder builder;
            builder
                .addVertex({ 0,0,0 }).addColor({ 1, 1, 1, 1 })
                .addVertex({ 1,0,0 }).addColor({ 1, 1, 1, 1 })
                .addVertex({ 0,1,0 }).addColor({ 1, 1, 1, 1 })
                .addTriangle(0, 1, 2);
            return rm.createMesh(TRIANGLE_MESH_NAME, builder);
            };

        ResourceManager::ProceduralResourceFactory<Mesh> quadMeshFabric = [](ResourceManager& rm) {
            MeshBuilder builder;
            std::array<math::Vector3, 4> positions =
            {
                math::Vector3{-0.5f, 0.0f, -0.5f},
                math::Vector3{-0.5f, 0.0f,  0.5f},
                math::Vector3{ 0.5f, 0.0f,  0.5f},
                math::Vector3{ 0.5f, 0.0f, -0.5f}
            };

            math::Vector3 normal{ 0.0f, 1.0f, 0.0f };

            vertex_meta::color_type color(1.0f, 1.0f, 1.0f, 1.0f);

            std::array<math::Vector2, 4> uvs = {
                math::Vector2{0.0f, 1.0f},
                math::Vector2{0.0f, 0.0f},
                math::Vector2{1.0f, 0.0f},
                math::Vector2{1.0f, 1.0f}
            };

            for (size_t i = 0; i < 4; ++i) {
                builder.addVertex(positions[i])
                    .addNormal(normal)
                    .addUV(uvs[i])
                    .addColor(color);
            }

            builder.addTriangle(0, 1, 2)
                .addTriangle(0, 2, 3);

            return rm.createMesh(QUAD_MESH_NAME, builder);
            };

        ResourceManager::ProceduralResourceFactory<Mesh> cubeMeshFabric = [](ResourceManager& rm) {
            MeshBuilder builder;
            const float hs = 0.5f; // half-size
            struct Face
            {
                math::Vector3 normal;
                vertex_meta::color_type color;
                std::array<math::Vector3, 4> positions;
            };
            std::array<Face, 6> faces =
            {
                Face{{0,0,1},{1,1,1,1},  {{ { -hs,-hs, hs }, {hs,-hs, hs}, {hs,hs, hs}, {-hs,hs, hs} } }},    // front
                Face{{0,0,-1},{1,1,1,1}, {{ {hs,-hs,-hs}, {-hs,-hs,-hs}, {-hs,hs,-hs}, {hs,hs,-hs}   }},},   // back
                Face{{-1,0,0},{1,1,1,1}, {{ {-hs,-hs,-hs}, {-hs,-hs, hs}, {-hs,hs, hs}, {-hs,hs,-hs} }},},   // left
                Face{{1,0,0},{1,1,1,1},  {{ {hs,-hs, hs}, {hs,-hs,-hs}, {hs,hs,-hs}, {hs,hs, hs}     }},},   // right
                Face{{0,1,0},{1,1,1,1},  {{ {-hs, hs, hs}, {hs, hs, hs}, {hs, hs,-hs}, {-hs, hs,-hs} }},},   // top
                Face{{0,-1,0},{1,1,1,1}, {{ {-hs,-hs,-hs}, {hs,-hs,-hs}, {hs,-hs, hs}, {-hs,-hs, hs} }},}    // bottom
            };
            uint32_t vertexOffset = 0;

            for (const auto& f : faces)
            {
                for (size_t i = 0; i < 4; ++i)
                {
                    builder.addVertex(f.positions[i]);
                    builder.addNormal(f.normal);
                    builder.addColor(f.color);
                }
                builder.addTriangle(vertexOffset + 0, vertexOffset + 1, vertexOffset + 2);
                builder.addTriangle(vertexOffset + 0, vertexOffset + 2, vertexOffset + 3);

                vertexOffset += 4;
            }
            return rm.createMesh(CUBE_MESH_NAME, builder);
            };

        ResourceManager::ProceduralResourceFactory<Mesh> sphereMeshFabric = [](ResourceManager& rm) {
            const int latBands = 16;
            const int longBands = 16;
            const float radius = 0.5f;
            MeshBuilder builder;

            for (int lat = 0; lat <= latBands; lat++)
            {
                float theta = lat * DirectX::XM_PI / latBands;
                float sinTheta = sinf(theta);
                float cosTheta = cosf(theta);
                for (int lon = 0; lon <= longBands; lon++)
                {
                    float phi = lon * 2.0f * DirectX::XM_PI / longBands;
                    float sinPhi = sinf(phi);
                    float cosPhi = cosf(phi);

                    math::Vector3 pos{ cosPhi * sinTheta * radius, cosTheta * radius, sinPhi * sinTheta * radius };
                    render::vertex_meta::color_type c{ 0.5f + 0.5f * pos.x, 0.5f + 0.5f * pos.y, 0.5f + 0.5f * pos.z,1.0f };
                    math::Vector3 normal = pos; // center in (0,0,0)
                    normal.normalize();
                    math::Vector2 uv{ static_cast<float>(lon) / longBands, static_cast<float>(lat) / latBands };

                    builder.addVertex(pos).addColor(c).addNormal(normal).addUV(uv);
                }
            }

            for (int lat = 0; lat < latBands; lat++)
            {
                for (int lon = 0; lon < longBands; lon++)
                {
                    int first = lat * (longBands + 1) + lon;
                    int second = first + longBands + 1;

                    builder.addTriangle(first, second, first + 1);
                    builder.addTriangle(second, second + 1, first + 1);
                }
            }

            return rm.createMesh(SPHERE_MESH_NAME, builder);
            };

        rm.registerProcedural(TEXTURE_SHADER_NAME, textureShaderFabric);
        rm.registerProcedural(TEXTURE_MATERIAL_NAME, textureMaterialFabric);

        rm.registerProcedural(DEFAULT_SHADER_NAME, defaultShaderFabric);
        rm.registerProcedural(DEFAULT_MATERIAL_NAME, defaultMaterialFabric);

        rm.registerProcedural(LINE_MESH_NAME, lineMeshFabric);
        rm.registerProcedural(TRIANGLE_MESH_NAME, triangleMeshFabric);
        rm.registerProcedural(QUAD_MESH_NAME, quadMeshFabric);
        rm.registerProcedural(CUBE_MESH_NAME, cubeMeshFabric);

        rm.registerProcedural(RAINBOW_SHADER_NAME, rainbowShaderFabric);
        rm.registerProcedural(RAINBOW_MATERIAL_NAME, rainbowMaterialFabric);

        rm.registerProcedural(SPHERE_MESH_NAME, sphereMeshFabric);

        return true;
    }

    ShaderHandle Primitives::getTextureShader()
    {
        return core::Services::get<ResourceManager>()->get<GraphicShader>(
            TEXTURE_SHADER_NAME);
    }

    MaterialHandle Primitives::getTextureMaterial()
    {
        return core::Services::get<ResourceManager>()->get<Material>(
            TEXTURE_MATERIAL_NAME);
    }

    ShaderHandle Primitives::getDefaultShader()
    {
        return core::Services::get<ResourceManager>()->get<GraphicShader>(
            DEFAULT_SHADER_NAME);
    }

    MaterialHandle Primitives::getDefaultMaterial()
    {
        return core::Services::get<ResourceManager>()->get<Material>(
            DEFAULT_MATERIAL_NAME);
    }

    ShaderHandle Primitives::getRainbowShader()
    {
        return core::Services::get<ResourceManager>()->get<GraphicShader>(
            RAINBOW_SHADER_NAME);
    }

    MaterialHandle Primitives::getRainbowMaterial()
    {
        return core::Services::get<ResourceManager>()->get<Material>(
            RAINBOW_MATERIAL_NAME);
    }

    MeshHandle Primitives::getLine()
    {
        return core::Services::get<ResourceManager>()->get<Mesh>(
            LINE_MESH_NAME);
    }

    MeshHandle Primitives::getTriangle()
    {
        return core::Services::get<ResourceManager>()->get<Mesh>(
            TRIANGLE_MESH_NAME);
    }

    MeshHandle Primitives::getQuad()
    {
        return core::Services::get<ResourceManager>()->get<Mesh>(
            QUAD_MESH_NAME);
    }

    MeshHandle Primitives::getCube()
    {
        return core::Services::get<ResourceManager>()->get<Mesh>(
            CUBE_MESH_NAME);
    }

    MeshHandle Primitives::getSphere()
    {
        return core::Services::get<ResourceManager>()->get<Mesh>(
            SPHERE_MESH_NAME);
    }
}