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
            event.scene.view<Transform, MeshFilter, MeshRenderer>()
                .each([&](Entity::ID id,
                    Transform& tr,
                    MeshFilter& mf,
                    MeshRenderer& mr)
                    {
                        auto* mesh = event.resources.getMesh(mf.mesh);
                        auto* material = event.resources.getMaterial(mr.material);
                        if (!mesh || !material) return;

                        // TODO: в константный буфер положить tr.worldMatrix()
                        mesh->draw(event.render, material);
                    });
        }

    };
}

#endif
