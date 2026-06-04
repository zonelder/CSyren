#ifndef __CSYREN_MESH_RENDER_SYSTEM__
#define __CSYREN_MESH_RENDER_SYSTEM__

#include "core/event_bus.h"
#include "core/context.h"
#include "core/system_base.h"
#include "core/scene.h"

#include "core/transform.h"
#include "mesh_filter.h"
#include "math/math.h"

using namespace csyren::core;
using namespace csyren::math;
using namespace csyren::render::components;
using namespace csyren::core::components;

namespace csyren
{
    class MeshRenderSystem : public core::System
    {
    public:
        explicit MeshRenderSystem() = default;

        void onFrame(core::ServiceContext& ctx) override
        {

            auto render = render::Renderer::instancePtr();
            auto scene = ctx.get<core::Scene>();
            auto& resources = render::ResourceManager::instance();

            struct EntityData { Transform* tr; Entity::ID id; };
            using MeshGroups = std::unordered_map<render::MeshHandle,std::vector<EntityData>>;
            using MaterialGroups = std::unordered_map<render::MaterialHandle, MeshGroups>;


            MaterialGroups batches;
            auto entityParams = render->getEntityVariableBuffer();
            for (auto [entt, tr, mf, mr] : scene->view<Transform, MeshFilter, MeshRenderer>())
            {
                batches[mr.material][mf.mesh].push_back({ &tr,entt });
            }

            for (auto& [matID, meshGroups] : batches)
            {
                auto* mat = resources.getMaterial(matID);
                if (!mat) continue;

                auto* shader = resources.getShader(mat->getShader());
                if (!shader) continue;
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
                        DirectX::XMStoreFloat4x4(&entityParams->worldMatrix, data.tr->world());
                        entityParams->entityID = data.id;
                        if (!render->bindEntity(cb)) continue;
                        mesh->draw(*render);
                    }
                }
            }
        }

    };
}

#endif
