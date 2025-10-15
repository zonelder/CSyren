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
            render::UploadRingBuffer* perEntityCB = event.render.getPerEntityCB();
            auto& keyboard = event.devices.keyboard();
            auto engineParams = event.render.getEngineVariableBuffer();
            auto entityParams = event.render.getEntityVariableBuffer();
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

            float moveSpeed = 0.01f;
            if (keyboard.isKeyDown(KeyCode::LShift))
                moveSpeed *= 5.0f;
            DirectX::XMFLOAT3 position =  DirectX::XMFLOAT3(0,0,0);
            if (keyboard.isKeyDown(KeyCode::W))
                position.y += moveSpeed;
            if (keyboard.isKeyDown(KeyCode::S))
                position.y -= moveSpeed;
            if (keyboard.isKeyDown(KeyCode::A))
                position.x -= moveSpeed;
            if (keyboard.isKeyDown(KeyCode::D))
                position.x += moveSpeed;
            if (keyboard.isKeyDown(KeyCode::Q))
                position.z -= moveSpeed;
            if (keyboard.isKeyDown(KeyCode::E))
                position.z += moveSpeed;

            DirectX::XMFLOAT4 color(r, g, b, 1.0f);

            event.scene.view<Transform, MeshFilter, MeshRenderer>()
                .each([&](Entity::ID id,
                    Transform& tr,
                    MeshFilter& mf,
                    MeshRenderer& mr)
                    {
                        auto* mesh = event.resources.getMesh(mf.mesh);

                        auto mat = event.resources.getMaterial(mr.material);
                        if (!mat || !mesh)
                            return;
                        auto shader = event.resources.getShader(mat->getShader());
                        if (!shader)
                            return;

                        tr.position += Vector3(position.x, position.y, position.z);
                        DirectX::XMFLOAT4X4 out;
                        DirectX::XMStoreFloat4x4(&out, tr.world());
                        entityParams->worldMatrix = out;
                        entityParams->entityID = id;
                        mat->setVector("tint", color);
                        if (!event.render.bindMaterial(event.resources,mr.material))
                            return;

                        if (!event.render.bindEntity(shader->getSemanticBuffer(render::details::CBufferUpdateType::Entity)))
                        {
                            return;
                        }

                        mesh->draw(event.render);
                    });
        }

    };
}

#endif
