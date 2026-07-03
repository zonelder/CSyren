#pragma once
#include "base_editor_window.h"
#include "core/services.h"
#include "core/system_manager.h"

namespace csyren::editor
{
    class SystemWindow : public BaseEditorWindow
    {
    public:
        SystemWindow() : BaseEditorWindow("systems") {}

        void onFrame() override
        {
            auto* systems = core::Services::get<core::SystemManager>();
            const auto& sysList = systems->list();
            constexpr uint32_t INVALID = std::numeric_limits<uint32_t>::max();
            std::pair<uint32_t, uint32_t> movePair{ INVALID ,INVALID };
            if (ImGui::BeginTable("SystemsOrder", 1, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY))
            {
                ImGui::TableSetupColumn("System", ImGuiTableColumnFlags_WidthStretch);
                ImGui::TableHeadersRow();

                for (size_t i = 0; i < sysList.size(); ++i)
                {
                    ImGui::TableNextRow();
                    ImGui::TableNextColumn();
                    auto& sys = sysList[i];

                    ImGui::PushID(static_cast<int>(i));

                    if (ImGui::Selectable(sys.name.c_str(), false))
                    {

                    }

                    if (ImGui::BeginDragDropSource())
                    {
                        ImGui::SetDragDropPayload("SYSTEM_ORDER", &i, sizeof(size_t));
                        ImGui::TextUnformatted(sys.name.c_str());
                        ImGui::EndDragDropSource();
                    }

                    if (ImGui::BeginDragDropTarget())
                    {
                        if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("SYSTEM_ORDER"))
                        {
                            size_t src_idx = *(const size_t*)payload->Data;
                            if (src_idx != i) {
                                systems->move(src_idx, i);
                            }
                        }
                        ImGui::EndDragDropTarget();
                    }

                    ImGui::PopID();
                }
                ImGui::EndTable();
            }
        }
    };
}