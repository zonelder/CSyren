#pragma once
#include "imGui/imgui.h"
#include <algorithm>

namespace csyren::editor
{
    struct WorkflowToolbarLayout
    {
        bool wrapTools;
        float documentWidth,height;
        static WorkflowToolbarLayout measure(float width,bool tools,const char* error)
        {
            const auto& style = ImGui::GetStyle();
            const auto text = [](const char* value) { return ImGui::CalcTextSize(value).x; };
            const float available = (std::max)(1.f,width-2*style.WindowPadding.x);
            const float controls = text("Play")+text("Stop")+4*style.FramePadding.x+3*style.ItemSpacing.x+text("|")+text("Playing")+2*style.ItemSpacing.x;
            const float radio = ImGui::GetFrameHeight()+style.ItemInnerSpacing.x;
            const float modes = tools ? text("Move (W)")+text("Rotate (E)")+text("Scale (R)")+3*radio+text("| F: focus")+5*style.ItemSpacing.x+text("|") : 0;
            const bool wrap = tools && available < controls+modes+text("Untitled *");
            const float document = (std::max)(1.f,available-controls-(wrap ? 0 : modes));
            const int rows = wrap ? 2 : 1;
            float height = 2*style.WindowPadding.y+rows*ImGui::GetFrameHeight()+(rows-1)*style.ItemSpacing.y;
            if (error && *error) height += style.ItemSpacing.y+ImGui::CalcTextSize(error,nullptr,false,available).y;
            return {wrap,document,height};
        }
    };
}
