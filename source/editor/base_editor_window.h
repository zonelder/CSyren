#pragma once
#include <string>
#include "editor_base.h"
#include "cstdmf/profiler.h"



namespace csyren::editor
{
    class BaseEditorWindow
    {
    public:
        BaseEditorWindow(const std::string& title) : _title(title), _mark(profiler::name("Window: " + title)) {}
        virtual ~BaseEditorWindow() = default;

        virtual void onInit() {}
        virtual void onFrame() = 0;
        virtual void onShutdown() {}

        void render()
        {
            if (!m_isOpen) return;
            const profiler::Scope scope(_mark);
            // Collapsed or a tab behind another one: nothing is drawn, nothing to build.
            if (ImGui::Begin(_title.c_str(), &m_isOpen)) onFrame();
            ImGui::End();
        }

        bool isOpen() const { return m_isOpen; }
        void setOpen(bool open) { m_isOpen = open; }

        std::string_view title() const noexcept
        {
            return _title;
        }

    protected:
        std::string _title;
        uint32_t _mark = 0;
        bool m_isOpen = true;
    };
}

