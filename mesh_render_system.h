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

        void draw(events::DrawEvent& event) override
        {
            ID3D12GraphicsCommandList* cmd = event.render.commandList();
            auto perFrameCB = event.render.getPerFrameCB();
            auto perEntityCB = event.render.getPerEntityCB();
            auto perMaterialCB = event.render.getPerMaterialCB();
            //auto perEntityBuffer = event.render.getPerEntityBuffer();

            struct EntityBuffer
            {
                DirectX::XMMATRIX world;
            } perEntityBuffer;

            event.scene.view<Transform, MeshFilter, MeshRenderer>()
                .each([&](Entity::ID id,
                    Transform& tr,
                    MeshFilter& mf,
                    MeshRenderer& mr)
                    {
                        auto* mesh = event.resources.getMesh(mf.mesh);
                        auto* material = event.resources.getMaterial(mr.material);
                        if (!mesh || !material) return;
                        auto* shader = event.resources.getShader(material->getShader());
                        if (!shader) return;

                        cmd->SetPipelineState(material->pso());
                        cmd->SetGraphicsRootSignature(shader->getRootSignature());

                        // Update per-entity constant buffer (world matrix)

                        
                        auto perFrameRoot = shader->getRootParameterIndex("PerFrame");
                        auto perEntityRoot = shader->getRootParameterIndex("PerObject");
                        if (perFrameRoot != UINT_MAX)
                        {
                            cmd->SetGraphicsRootConstantBufferView(0, perFrameCB->gpuAddress());
                        }

                        if (perEntityRoot != UINT_MAX)
                        {
                            perEntityBuffer.world = tr.world();
                            auto gpuAdress = perEntityCB->update(&perEntityBuffer, sizeof(EntityBuffer));
                            cmd->SetGraphicsRootConstantBufferView(1, gpuAdress);
                        }

                        //cmd->SetGraphicsRootConstantBufferView(2, perMaterialCB->gpuAddress());

                        mesh->draw(event.render);
                    });
        }

    };
}

#endif
