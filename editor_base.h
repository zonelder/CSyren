#pragma once
#include "core/meta_any.h"

#include "imGui/imgui.h"
#include "imGui/backends/imgui_impl_dx12.h"
#include "imGui/backends/imgui_impl_win32.h"

#include "cstdmf/assert_helpler.h"

namespace csyren::editor
{
	class IEditor
	{
	public:
        virtual ~IEditor() = default;
		virtual void draw(core::reflection::MetaAny& any, const char* label)  = 0;
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
            return it == _metaMap.end() ? _defaultEditor : it->second;
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
        static std::unordered_map<core::reflection::MetaType*, IEditor*> _metaMap;
    };
}

namespace csyren::editor::details
{
    class DefaultEditor : public IEditor
    {
    public:
        void draw(core::reflection::MetaAny& any, const char* label) override
        {
            if (!any) return;
            const float indentLevel = 1;
            float indent = indentLevel * 16.0f;
            ImGui::Indent(indent);
            any.forEachField([](auto literal, core::reflection::MetaAny any) {

                auto fieldEditor = EditorRegistry::get(any.type());
                fieldEditor->draw(any, literal.str.data());
                });

            ImGui::Unindent(indent);
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