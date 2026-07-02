#pragma once

#include "component_editor.h"
#include "core/transform.h"

namespace csyren::editor
{
    class TransformEditor : public ComponentEditor
    {
        using comp_type = core::_components::Transform;
    public:
        const char* name() const override { return "Transform"; }

        void render(core::Entity* entity, core::Scene* scene) override
        {
            auto id = entity->id;
            if (!scene->hasComponent<comp_type>(id)) return;

            auto transform = scene->getComponent<comp_type>(id);
            if (!transform) return;

            bool changed = false;

            // Position
            if (ImGui::DragFloat3("Position", &transform->position.x, 0.1f))
            {
                changed = true;
            }

            math::Vector3 euler = transform->rotation.eulerAngles();

            if (ImGui::DragFloat3("Rotation (Euler)", &euler.x, 0.1f))
            {
                transform->rotation = math::Quaternion::euler(euler);
                changed = true;
            }

            // Scale
            if (ImGui::DragFloat3("Scale", &transform->scale.x, 0.1f, 0.01f, 100.0f))
            {
                changed = true;
            }

            if (changed)
            {
               // scene->markDirty(); // если у вас есть такая система
            }
        }
    };
}