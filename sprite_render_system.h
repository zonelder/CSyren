#pragma once
#include "core/event_bus.h"
#include "core/context.h"
#include "core/system_base.h"
#include "core/scene.h"

#include "transform.h"
#include "sprite_renderer.h"

#include <algorithm>

using namespace csyren::core;
using namespace csyren::components;

namespace csyren
{
	class SpriteRenderSystem : public System
	{
	public:
        explicit SpriteRenderSystem() = default;

        void draw(events::DrawEvent& event) override
        {
            ID3D12GraphicsCommandList* cmd = event.render.commandList();
            auto perFrameCB = event.render.getPerFrameCB();
            auto perEntityCB = event.render.getPerEntityCB();
            auto perMaterialCB = event.render.getPerMaterialCB();
            auto perEntityBuffer = event.render.getPerEntityBuffer();



            struct EntityData
            {
                int depth;//copy of SpriteRenderer::depth
                SpriteRenderer& sr;
                Transform& tr;
            };

            std::vector<EntityData> entitiesToRender;
            auto view = event.scene.view<Transform, SpriteRenderer>();

            //gather all possible entities;
            for (auto [entt, tr, sprite] : view)
            {
                entitiesToRender.emplace_back( EntityData{ sprite.depth,sprite,tr });
            }
            if (entitiesToRender.empty())
            {
                return;
            }
            auto meshHandle = render::Primitives::getQuad(event.resources);
            auto mesh = event.resources.getMesh(meshHandle);

            if (!mesh)
            {
                log::error("SpriteRenderSystem: cant load default mesh for sprite renderer");
                return;
            }

            std::sort(entitiesToRender.begin(), entitiesToRender.end(), [](const EntityData& a, const EntityData& b)
                {
                    return a.depth < b.depth;
                });

            for (const auto& data : entitiesToRender)
            {
                auto* material = event.resources.getMaterial(data.sr.material);
                auto* texture = event.resources.getTexture(data.sr.texture);
                if (!material || !texture)
                    continue;

                auto* shader = event.resources.getShader(material->getShader());

                cmd->SetPipelineState(material->pso());
                cmd->SetGraphicsRootSignature(shader->getRootSignature());

                //set contsant and draw mesh

                mesh->draw(event.render);
            }

        }

	};
}
