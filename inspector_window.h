#pragma once

#include "base_editor_window.h"
#include "core/services.h"
#include "core/scene.h"

#include "editor_selection.h"


namespace csyren::editor
{
    class InspectorWindow : public BaseEditorWindow
    {
    public:
        InspectorWindow() : BaseEditorWindow("inspector") {}

        void onFrame() override
        {
            auto* scene = core::Services::get<core::Scene>();
            auto* selector = core::Services::get<EditorSelection>();

            if (!selector->hasSelection())
            {
                ImGui::TextDisabled("No entity selected");
                return;
            }

            auto entt = scene->entities().try_get(selector->selected());
            if (!entt)
            {
                selector->clearSelection();
                ImGui::TextDisabled("No entity selected");
                return;
            }

            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.7f, 0.9f, 1.0f, 1.0f));
            ImGui::Text("%s", entt->name.c_str());
            ImGui::PopStyleColor();

            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Spacing();
        }
    };
}