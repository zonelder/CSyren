#ifndef __CSYREN_MESH_RENDER_SYSTEM__
#define __CSYREN_MESH_RENDER_SYSTEM__

#include "core/event_bus.h"
#include "core/system_base.h"
#include "core/scene.h"
#include "core/services.h"

#include "core/transform.h"
#include "core/transform_hierarchy.h"
#include "mesh_filter.h"
#include "math/math.h"
#include "core/registration.h"

namespace csyren
{
    // Geometry (GBuffer) pass of the deferred pipeline: draws every entity whose
    // material shader writes the three GBuffer targets (see shaders/gbuffer.hlsli).
    // Single-target (forward/unlit/transparent) materials are drawn later by
    // ForwardRenderSystem on top of the lit HDR image.
    class MeshRenderSystem : public core::System
    {
    public:
        core::TaskDescription describe() const override {
            core::TaskDescription d; d.phase = "render";
            d.gpuScope = core::GpuTaskScope::View;
            d.read<core::Transform>().read<render::MeshFilter>().read<render::MeshRenderer>();
            d.readResource<render::ResourceManager>().readResource<core::SceneEntities>();
            d.gpu("scene.gbuffer0", D3D12_RESOURCE_STATE_RENDER_TARGET, core::AccessMode::Write);
            d.gpu("scene.gbuffer1", D3D12_RESOURCE_STATE_RENDER_TARGET, core::AccessMode::Write);
            d.gpu("scene.gbuffer2", D3D12_RESOURCE_STATE_RENDER_TARGET, core::AccessMode::Write);
            d.gpu("scene.depth", D3D12_RESOURCE_STATE_DEPTH_WRITE, core::AccessMode::Write); return d;
        }

        explicit MeshRenderSystem() = default;

        void execute() override
        {
            using ctx = core::Services;
            auto render = ctx::get<render::Renderer>();
            auto scene = &this->scene();
            auto& resources = *ctx::get<const render::ResourceManager>();
            const auto& worlds = render->worldTransforms();

            struct EntityData { core::Entity::ID id; };
            using MeshGroups = std::unordered_map<render::MeshHandle,std::vector<EntityData>>;
            using MaterialGroups = std::unordered_map<render::MaterialHandle, MeshGroups>;


            MaterialGroups batches;
            auto entityParams = render->getEntityVariableBuffer();
            for (auto [entt, tr, mf, mr] : scene->view<const core::Transform, const render::MeshFilter, const render::MeshRenderer>())
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

                    if (!render->bindMaterial(resources, matID,mesh->getLayout())) continue;

                    mesh->bind(*render);

                    for (auto& data : entities)
                    {
                        //TODO instancing look pretty well here.
                        DirectX::XMStoreFloat4x4(&entityParams->worldMatrix, worlds.matrix(data.id));
                        entityParams->entityID = data.id;
                        if (!render->bindEntity(cb)) continue;
                        mesh->draw(*render);
                    }
                }
            }
        }

    };

    CS_SYSTEM(MeshRenderSystem);
}

#endif
