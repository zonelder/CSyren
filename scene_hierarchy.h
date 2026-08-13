#pragma once
#include "base_editor_window.h"
#include "core/services.h"
#include "core/scene.h"


#include "editor_selection.h"

namespace csyren::editor
{
    class SceneHierarchyWindow : public BaseEditorWindow
    {
    public:
        SceneHierarchyWindow() : BaseEditorWindow("Scene Hierarchy") {}

        void drawEntityTree(core::Scene* scene, EditorSelection* selection, core::Entity::ID entityId)
        {
            auto* entt = scene->entities().try_get(entityId);
            if (!entt) return;

            ImGui::PushID(static_cast<int>(entityId));

            bool isSelected = selection->isSelected(entityId);
            bool hasChildren = !entt->children.empty();

            if (hasChildren)
            {
                // TreeNode дл€ элементов с детьми
                ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow
                    | ImGuiTreeNodeFlags_SpanAvailWidth;
                if (isSelected) flags |= ImGuiTreeNodeFlags_Selected;

                bool open = ImGui::TreeNodeEx(entt->name.c_str(), flags);

                if (ImGui::IsItemClicked())
                {
                    selection->select(entityId);
                    log::info("select entt '{}' (id={})", entt->name, entityId);
                }

                if (open)
                {
                    for (core::Entity::ID childId : entt->children)
                    {
                        drawEntityTree(scene, selection, childId);
                    }
                    ImGui::TreePop();
                }
            }
            else
            {
                // Leaf node Ч используем Selectable
                if (ImGui::Selectable(entt->name.c_str(), isSelected,
                    0))
                {
                    selection->select(entityId);
                    log::info("select entt '{}' (id={})", entt->name, entityId);
                }
            }

            ImGui::PopID();
        }
        void onFrame() override
        {
            auto* scene = core::Services::get<core::Scene>();
            auto* selection = core::Services::get<EditorSelection>();
            drawEntityTree(scene, selection, core::EntityManager::ROOT_PARENT);
        }
    };
}