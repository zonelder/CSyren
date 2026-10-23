#include "inspector_window.h"

#include "core/services.h"
#include "core/scene.h"
#include "editor_selection.h"
#include "core/meta_any.h"
#include "core/unknown_components.h"
#include <algorithm>

namespace csyren::editor
{
    class EntityInspectorWidget
    {
    public:

        static void draw(core::Scene& scene, const core::Entity* entt)
        {
            if (!entt) return;
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.7f, 0.9f, 1.0f, 1.0f));
            ImGui::Text("Entity ID: %u (generation %u)", core::Entity::slot(entt->id), core::Entity::generation(entt->id));
            ImGui::Text("Name: %s", entt->name.c_str());

            auto newParent = entt->parent;
            auto pEntt = scene.entities().try_get(newParent);
            auto preview = pEntt != nullptr ? pEntt->name.c_str() : "(None)";
            ImGui::Text("Parent: %s", preview);
            ImGui::PopStyleColor();
        }
    };
}


void csyren::editor::InspectorWindow::onFrame()
{
    auto* session = core::Services::get<SceneSession>();
    auto* scene = &session->editorScene();
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

    EntityInspectorWidget::draw(*scene, entt);
    ImGui::BeginDisabled(session->playing() || entt->id == core::EntityManager::ROOT_PARENT);



    ImGui::Spacing();

    const auto enqueueComponent = [&](SceneOperation operation, size_t family) {
        session->finishEdit();
        SceneRequest request{ operation, session->documentId() };
        request.entity = session->keyFor(entt->id); request.component = family;
        session->enqueue(std::move(request));
    };
    if (ImGui::Button("Add Component")) ImGui::OpenPopup("Add component");
    if (ImGui::BeginPopup("Add component"))
    {
        std::vector<const core::reflection::ComponentMeta*> available;
        for (const auto& [family, info] : core::reflection::ComponentRegistry::getAll())
            if (info.createValue && info.addValue && info.metaType && info.metaType->archive.save &&
                info.metaType->archive.load && !info.has(scene, entt->id)) available.push_back(&info);
        std::sort(available.begin(), available.end(), [](const auto* a, const auto* b) { return a->name < b->name; });
        if (available.empty()) ImGui::TextDisabled("All available components are already added");
        for (const auto* info : available)
            if (ImGui::MenuItem(std::string(info->name).c_str())) enqueueComponent(SceneOperation::AddComponent, info->family);
        ImGui::EndPopup();
    }


    for (auto i : entt->componentView())
    {
        if (i == core::reflection::ComponentFamily::getID<core::UnknownComponents>())
        {
            // Read only: kept from the file and saved back unchanged.
            const auto* unknown = static_cast<const core::UnknownComponents*>(scene->getComponentRaw(entt->id, i));
            if (unknown && !unknown->entries.empty() && ImGui::CollapsingHeader("Unknown components", ImGuiTreeNodeFlags_DefaultOpen))
            {
                ImGui::TextDisabled("Types this build does not know: kept and saved unchanged.");
                for (const auto& entry : unknown->entries) ImGui::BulletText("%s", entry.name.c_str());
            }
            continue;
        }
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
        bool keep = true;
        const auto* info = core::reflection::ComponentRegistry::get(i);
        const bool removable = info && info->createValue && info->addValue && metaType->archive.save && metaType->archive.load;
        const bool open = ImGui::CollapsingHeader(metaType->id.str.data(), removable ? &keep : nullptr, ImGuiTreeNodeFlags_DefaultOpen);
        if (!keep) enqueueComponent(SceneOperation::RemoveComponent, i);
        if (open && keep)
        {
            auto editor = EditorRegistry::get(metaType);
            ValueEditContext context{ *session, session->keyFor(entt->id), i, {} };
            editor->draw(compAny, metaType->id.str.data(), context);
        }
        ImGui::PopID();

    }
    ImGui::EndDisabled();
}
