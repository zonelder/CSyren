#pragma once

#include "component_editor.h"
#include "core/transform.h"

namespace csyren::editor
{
    class TransformEditor : public ComponentEditor<core::Transform>
    {
    public:
        const char* name() const override { return "Transform"; }

        void onInspect(comp_type& transform, core::Entity::ID entity, core::Scene* scene) override
        {
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