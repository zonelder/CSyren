#pragma once
#include <string>

#include "imGui/imgui.h"
#include "imGui/backends/imgui_impl_dx12.h"
#include "imGui/backends/imgui_impl_win32.h"


namespace csyren::editor
{
    class BaseEditorWindow
    {
    public:
        BaseEditorWindow(const std::string& title) : _title(title) {}
        virtual ~BaseEditorWindow() = default;

        virtual void onInit() {}
        virtual void onFrame() = 0;
        virtual void onShutdown() {}

        void render()
        {
            if (!m_isOpen) return;

            ImGui::Begin(_title.c_str(), &m_isOpen);
            onFrame();
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
        bool m_isOpen = true;
    };
}

