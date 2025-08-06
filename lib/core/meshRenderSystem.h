#ifndef __CSYREN_MESH_RENDER_SYSTEM__
#define __CSYREN_MESH_RENDER_SYSTEM__

#include "core/event_bus.h"
#include "core/context.h"

#include "transform.h"
#include "mesh_filter.h"
#include "entity.h"

using namespace csyren::core::events;
using namespace csyren::components;

namespace csyren::systems
{
    class MeshRenderSystem
    {
    public:
        explicit MeshRenderSystem(EventBus2 & bus)
        {
            bus.subscribe<DrawEvent>([this](DrawEvent& e)
                {
                    onDraw(e);
                });
        }

    private:
        void onDraw(DrawEvent& e)
        {
            e.scene.view<Transform, MeshFilter, MeshRenderer>()
                .each([&](core::Entity::ID id,
                    Transform& tr,
                    MeshFilter& mf,
                    MeshRenderer& mr)
                    {
                        auto* mesh = e.resources.getMesh(mf.mesh);
                        auto* material = e.resources.getMaterial(mr.material);
                        if (!mesh || !material) return;

                        // TODO: в константный буфер положить tr.worldMatrix()
                        mesh->draw(e.render, material);
                    });
        }
    };
}

#endif
