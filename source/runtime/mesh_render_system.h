#ifndef __CSYREN_MESH_RENDER_SYSTEM__
#define __CSYREN_MESH_RENDER_SYSTEM__

#include <array>
#include "core/event_bus.h"
#include "core/typed_system.h"

#include "core/transform.h"
#include "core/transform_hierarchy.h"
#include "mesh_filter.h"
#include "math/math.h"
#include "core/registration.h"
#include "dx12_graphic/renderer.h"
#include "dx12_graphic/resource_manager.h"
#include "dx12_graphic/recorder.h"

CS_STRICT_SYSTEMS_BEGIN
namespace csyren
{
    // Geometry (GBuffer) pass of the deferred pipeline: draws every entity whose
    // material shader writes the three GBuffer targets (see shaders/gbuffer.hlsli).
    // Single-target (forward/unlit/transparent) materials are drawn later by
    // ForwardRenderSystem on top of the lit HDR image.
    class MeshRenderSystem : public core::TypedSystem<MeshRenderSystem>
    {
    public:
        static constexpr auto phase = "render";
        static constexpr core::GpuTaskScope gpuScope = core::GpuTaskScope::View;
        static constexpr std::array gpu{
            core::GpuUse{ "scene.gbuffer0", D3D12_RESOURCE_STATE_RENDER_TARGET, core::AccessMode::Write },
            core::GpuUse{ "scene.gbuffer1", D3D12_RESOURCE_STATE_RENDER_TARGET, core::AccessMode::Write },
            core::GpuUse{ "scene.gbuffer2", D3D12_RESOURCE_STATE_RENDER_TARGET, core::AccessMode::Write },
            core::GpuUse{ "scene.depth", D3D12_RESOURCE_STATE_DEPTH_WRITE, core::AccessMode::Write },
        };

        explicit MeshRenderSystem() = default;

        // World matrices come from the renderer's per-frame copy of the Transforms.
        using Meshes = core::Select<core::Transform, render::MeshFilter, render::MeshRenderer>;

        void execute(render::Recorder renderer, Meshes meshes, core::Res<const render::ResourceManager> resourceManager)
        {
            auto& resources = *resourceManager;
            const auto& worlds = renderer->worldTransforms();

            struct EntityData { core::Entity::ID id; };
            using MeshGroups = std::unordered_map<render::MeshHandle,std::vector<EntityData>>;
            using MaterialGroups = std::unordered_map<render::MaterialHandle, MeshGroups>;


            MaterialGroups batches;
            auto entityParams = renderer->getEntityVariableBuffer();
            for (auto [entt, tr, mf, mr] : meshes)
            {
                batches[mr.material][mf.mesh].push_back({ entt });
            }

            for (auto& [matID, meshGroups] : batches)
            {
                auto* mat = resources.getMaterial(matID);
                if (!mat) continue;

                auto* shader = resources.getShader(mat->getShader());
                if (!shader || !shader->isDeferred()) continue; // forward materials: ForwardRenderSystem
                auto* cb = shader->getSemanticBuffer(render::details::CBufferUpdateType::Entity);

                for (auto& [meshID, entities] : meshGroups)
                {
                    auto* mesh = resources.getMesh(meshID);
                    if (!mesh) continue;

                    if (!renderer->bindMaterial(resources, matID,mesh->getLayout())) continue;

                    mesh->bind(*renderer);

                    for (auto& data : entities)
                    {
                        //TODO instancing look pretty well here.
                        entityParams->setWorld(worlds.matrix(data.id));
                        entityParams->entityID = core::Entity::slot(data.id);
                        if (!renderer->bindEntity(cb)) continue;
                        mesh->draw(*renderer);
                    }
                }
            }
        }

    };

    CS_SYSTEM(MeshRenderSystem);
}
CS_STRICT_SYSTEMS_END

#endif
