#include "inspector_window.h"

#include "core/services.h"
#include "core/scene.h"
#include "editor_selection.h"
#include "core/meta_any.h"

void csyren::editor::InspectorWindow::onFrame()
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


    for (auto i : entt->componentView())
    {
        auto metaType = core::reflection::resolve(i);
        if (!metaType)
        {
            ImGui::Separator();
            ImGui::Spacing();
            ImGui::Text("unknown comp %d", i);
            ImGui::Spacing();
            continue;
        }

        auto raw = scene->getComponentRaw(entt->id, i);
        core::reflection::MetaAny compAny{ raw, metaType };

        ImGui::PushID(static_cast<int>(i));
        if (ImGui::CollapsingHeader(metaType->id.str.data(), ImGuiTreeNodeFlags_DefaultOpen))
        {
            auto editor = EditorRegistry::get(metaType);
            editor->draw(compAny, metaType->id.str.data());
        }
        ImGui::PopID();

    }
}
