#pragma once
#include "base_editor_window.h"
#include "core/services.h"
#include "core/scene.h"


namespace csyren::editor
{
    class SceneHierarchyWindow : public BaseEditorWindow
    {
    public:
        SceneHierarchyWindow() : BaseEditorWindow("Scene Hierarchy") {}

        void onFrame() override
        {
            auto* scene = core::Services::get<core::Scene>();

            for (const auto& entt: scene->entities())
            {
                if (ImGui::Selectable(entt.name.c_str())) {
                    log::info("select entt {}", entt.name);
                }
            }
        }
    };
}