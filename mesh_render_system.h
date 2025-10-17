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

                        DirectX::XMFLOAT4X4 out;
                        DirectX::XMStoreFloat4x4(&out, tr.world());
                        entityParams->worldMatrix = out;
                        entityParams->entityID = id;
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
