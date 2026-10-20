#pragma once
#include "core/meta_any.h"
#include "scene_session.h"

#include "imGui/imgui.h"
#include "imGui/backends/imgui_impl_dx12.h"
#include "imGui/backends/imgui_impl_win32.h"

#include "cstdmf/assert_helpler.h"

namespace csyren::editor
{
    struct ValueEditContext
    {
        SceneSession& session;
        EntityKey entity;
        size_t family;
        FieldPath path;
        template<class T, class Widget> void draw(T& value, Widget widget)
        {
            const T before = value;
            const bool changed = widget();
            if (changed) session.preview(entity, family, path, before, value, false);
            if (ImGui::IsKeyPressed(ImGuiKey_Escape, false) && (ImGui::IsItemActive() || ImGui::IsItemDeactivated()))
                session.cancelEdit();
            else if (ImGui::IsItemDeactivated() || (changed && !ImGui::IsItemActive())) session.finishEdit();
        }
        // For editors that only know the reflected type: the value is captured
        // through its archive before the widget runs.
        template<class Widget> void drawArchived(core::reflection::MetaAny& value, Widget widget)
        {
            const auto* type = value.type();
            auto before = ArchivedFieldEdit::capture(*type, value.raw());
            const bool changed = widget();
            if (changed) session.previewArchived(entity, family, path, *type, std::move(before), ArchivedFieldEdit::capture(*type, value.raw()), false);
            if (ImGui::IsKeyPressed(ImGuiKey_Escape, false) && (ImGui::IsItemActive() || ImGui::IsItemDeactivated()))
                session.cancelEdit();
            else if (ImGui::IsItemDeactivated() || (changed && !ImGui::IsItemActive())) session.finishEdit();
        }
    };
	class IEditor
	{
	public:
        virtual ~IEditor() = default;
		virtual void draw(core::reflection::MetaAny& any, const char* label, ValueEditContext&) = 0;
	};


    class EditorRegistry
    {
        struct PendingRegistration
        {
            void (*resolveAndRegister)();
        };
    public:
        static void add(core::reflection::MetaType* type, IEditor* editor)
        {
            auto it = _metaMap.find(type);
            CS_DEBUG_ASSERT_MSG(it == _metaMap.end(), "EditorRegistry: attempt to register editor, but we already have defined editor.\n");
            _metaMap[type] = editor;
        }

        static IEditor* get(core::reflection::MetaType* type)
        {
            auto it = _metaMap.find(type);
            if (it != _metaMap.end()) return it->second;
            // Every reflected enum / mask / named enum gets the generic editor.
            return type && type->enumInfo ? _enumEditor : _defaultEditor;
        }

        static void addPending(void(*resolveFn)())
        {
            getPending().push_back({ resolveFn });
        }

        static void resolveAll()
        {
            for (auto& reg : getPending())
                reg.resolveAndRegister();
            getPending().clear();
        }
    private:
        static std::vector<PendingRegistration>& getPending()
        {
            static std::vector<PendingRegistration> pending;
            return pending;
        }
        static IEditor* _defaultEditor;
        static IEditor* _enumEditor;
        static std::unordered_map<core::reflection::MetaType*, IEditor*> _metaMap;
    };
}

namespace csyren::editor::details
{
    class DefaultEditor : public IEditor
    {
    public:
        void draw(core::reflection::MetaAny& any, const char* label, ValueEditContext& context) override
        {
            if (!any) return;
            const float indentLevel = 1;
            float indent = indentLevel * 16.0f;
            ImGui::Indent(indent);
            using namespace core::reflection;
            for (const auto& field : any.type()->data)
            {
                if (!hasFlag(field.flags, FieldFlags::EditorVisible) || !field.type || !field.get) continue;
                MetaAny value{ field.get(any.raw()), field.type };
                auto child = context;
                child.path.push_back(field.id.id);
                ImGui::PushID(static_cast<int>(field.id.id));
                ImGui::BeginDisabled(hasFlag(field.flags, FieldFlags::ReadOnly) || hasFlag(field.flags, FieldFlags::Transient));
                EditorRegistry::get(field.type)->draw(value, field.name.data(), child);
                ImGui::EndDisabled();
                ImGui::PopID();
            }

            ImGui::Unindent(indent);
        }
    };
}


namespace csyren::editor::details
{
    // Combo for a single value, checkbox list for MaskOf<E>.
    class EnumEditor : public IEditor
    {
    public:
        void draw(core::reflection::MetaAny& any, const char* label, ValueEditContext& context) override
        {
            using namespace core::reflection;
            if (!any || !any.type()->enumInfo) return;
            const MetaEnum& info = *any.type()->enumInfo;
            void* raw = any.raw();
            const auto entries = info.list();
            const auto current = info.label(raw);
            context.drawArchived(any, [&] {
                bool changed = false;
                if (!ImGui::BeginCombo(label, current.c_str())) return false;
                if (info.isMask())
                {
                    for (const auto& entry : entries)
                    {
                        bool on = info.isSelected(raw, entry);
                        if (ImGui::Checkbox(entry.name.c_str(), &on)) { info.setFlag(raw, entry, on); changed = true; }
                    }
                    ImGui::Separator();
                    if (ImGui::SmallButton("All")) { info.setValue(raw, static_cast<int64_t>(info.allBits())); changed = true; }
                    ImGui::SameLine();
                    if (ImGui::SmallButton("None")) { info.setValue(raw, 0); changed = true; }
                }
                else
                {
                    if (entries.empty()) ImGui::TextDisabled("(no values registered)");
                    for (const auto& entry : entries)
                    {
                        const bool selected = info.isSelected(raw, entry);
                        if (ImGui::Selectable(entry.name.c_str(), selected) && !selected) { info.select(raw, entry); changed = true; }
                        if (selected) ImGui::SetItemDefaultFocus();
                    }
                }
                ImGui::EndCombo();
                return changed;
            });
        }
    };
}

#define REGISTER_EDITOR(Type, EditorClass)                                      \
    static EditorClass _editor_instance_##Type;                                 \
    static void _editor_resolve_##Type() {                                      \
        auto* meta = ::csyren::core::reflection::resolve<Type>();               \
        ::csyren::editor::EditorRegistry::add(                                  \
            meta, &_editor_instance_##Type);                                    \
    }                                                                           \
    static struct _EditorRegistrar_##Type {                                     \
        _EditorRegistrar_##Type() {                                             \
            ::csyren::editor::EditorRegistry::addPending(&_editor_resolve_##Type); \
        }                                                                       \
    } _editor_registrar_##Type##_instance;
