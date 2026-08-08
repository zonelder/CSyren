#pragma once

#include "base_editor_window.h"


namespace csyren::editor
{
    class InspectorWindow : public BaseEditorWindow
    {
    public:
        InspectorWindow() : BaseEditorWindow("inspector") {}

        void onInit() override
        {

        }

        void onFrame() override;
    private:
    };
}