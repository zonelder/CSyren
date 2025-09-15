#ifndef __CSYREN_MESH_RENDER_SYSTEM__
#define __CSYREN_MESH_RENDER_SYSTEM__

#include "core/event_bus.h"
#include "core/context.h"
#include "core/system_base.h"
#include "core/scene.h"

#include "transform.h"
#include "mesh_filter.h"
#include "math/math.h"


using namespace csyren::core;
using namespace csyren::math;
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
            render::UploadRingBuffer* perEntityCB = event.render.getPerEntityCB();
            auto perMaterialCB = event.render.getPerMaterialCB();
            //auto perEntityBuffer = event.render.getPerEntityBuffer();
            auto& keyboard = event.devices.keyboard();

            Vector4 color;
            using KeyCode = input::KeyCode;
            float r = 0, g = 0, b = 0;
            if (keyboard.isKeyDown(KeyCode::Q))
            {
                r = 1.0f;
            }
            if (keyboard.isKeyDown(KeyCode::W))
            {
                g = 1.0f;
            }

            if (keyboard.isKeyDown(KeyCode::E))
            {
                b = 1.0f;
            }
            color = Vector4(r, g, b, 1);
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
                        if (perFrameRoot != UINT_MAX)
                        {
                            cmd->SetGraphicsRootConstantBufferView(0, perFrameCB->gpuAddress());
                        }

                        shader->setMatrix("world", tr.world());
                        shader->setVector("tint", color);
                        shader->commit(cmd, *perEntityCB);

                        //cmd->SetGraphicsRootConstantBufferView(2, perMaterialCB->gpuAddress());

                        mesh->draw(event.render);
                    });
        }

    };
}

#endif
