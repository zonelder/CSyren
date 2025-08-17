#ifndef __CSYREN_MESH_RENDER_SYSTEM__
#define __CSYREN_MESH_RENDER_SYSTEM__

#include "core/event_bus.h"
#include "core/context.h"
#include "core/system_base.h"
#include "core/scene.h"

#include "transform.h"
#include "mesh_filter.h"


using namespace csyren::core;
using namespace csyren::components;

namespace csyren
{
    class MeshRenderSystem : public core::System
    {
    public:
        explicit MeshRenderSystem() = default;

        void draw(events::DrawEvent& event)
        {
            ID3D12GraphicsCommandList* cmd = event.render.commandList();
            auto perFrameCB = event.render.getPerFrameCB();
            auto perEntityCB = event.render.getPerEntityCB();
            auto perMaterialCB = event.render.getPerMaterialCB();
            
            auto perEntityBuffer = event.render.getPerEntityBuffer();

            perFrameCB->update(event.render.getPerFrameBuffer(), sizeof(render::PerFrameBuffer));
            
            event.scene.view<Transform, MeshFilter, MeshRenderer>()
                .each([&](Entity::ID id,
                    Transform& tr,
                    MeshFilter& mf,
                    MeshRenderer& mr)
                    {
                        auto* mesh = event.resources.getMesh(mf.mesh);
                        auto* material = event.resources.getMaterial(mr.material);
                        if (!mesh || !material) return;

                        cmd->SetPipelineState(material->pso());
                        cmd->SetGraphicsRootSignature(material->rootSig());

                        // Update per-entity constant buffer (world matrix)
                        perEntityBuffer->world = tr.world();
                        perEntityCB->update(perEntityBuffer, sizeof(render::PerEntityBuffer));
                        

                        cmd->SetGraphicsRootConstantBufferView(0, perFrameCB->gpuAddress());
                        cmd->SetGraphicsRootConstantBufferView(1, perEntityCB->gpuAddress());
                        cmd->SetGraphicsRootConstantBufferView(2, perMaterialCB->gpuAddress());

                        mesh->draw(event.render);
                    });
        }

    };
}

#endif
