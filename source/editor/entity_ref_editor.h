#pragma once
// Inspector editor for core::EntityRef fields: a list of the scene's entities.
// "None" clears the reference; "(missing)" shows one whose entity was destroyed.
// The edit records the targets' editor keys (EntityRefFieldEdit), so undo and
// redo work after the scene is rebuilt.
#include "editor_base.h"
#include "core/entity_ref.h"
#include <string>

namespace csyren::editor
{
    class EntityRefEditor : public IEditor
    {
    public:
        void draw(core::reflection::MetaAny& any, const char* label, ValueEditContext& context) override
        {
            auto& value = any.get<core::EntityRef>();
            auto& scene = context.session.editorScene();
            const auto* target = value.empty() ? nullptr : scene.entities().try_get(value.id);
            const std::string current = value.empty() ? "None" : target ? title(*target) : "(missing)";
            if (!ImGui::BeginCombo(label, current.c_str())) return;
            if (ImGui::IsWindowAppearing()) { _filter.Clear(); ImGui::SetKeyboardFocusHere(); }
            _filter.Draw("##filter");
            auto chosen = value.id;
            bool picked = false;
            if (ImGui::Selectable("None", value.empty())) { chosen = core::Entity::invalidID; picked = true; }
            for (const auto& entity : scene.entities())
            {
                if (entity.id == core::EntityManager::ROOT_PARENT) continue;
                const auto name = title(entity);
                if (!_filter.PassFilter(name.c_str())) continue;
                ImGui::PushID(static_cast<int>(core::Entity::slot(entity.id)));
                const bool selected = entity.id == value.id;
                if (ImGui::Selectable(name.c_str(), selected)) { chosen = entity.id; picked = true; }
                if (selected) ImGui::SetItemDefaultFocus();
                ImGui::PopID();
            }
            ImGui::EndCombo();
            if (!picked || chosen == value.id) return;
            const auto before = value.id;
            value.id = chosen;
            context.session.previewEntityRef(context.entity, context.family, context.path, before, chosen);
        }
    private:
        static std::string title(const core::Entity& entity)
        {
            return entity.name + "  #" + std::to_string(core::Entity::slot(entity.id));
        }
        ImGuiTextFilter _filter;
    };
}
