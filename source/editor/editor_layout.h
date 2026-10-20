#pragma once
#include "imGui/imgui.h"
#include "imGui/imgui_internal.h"

namespace csyren::editor
{
    class EditorDockLayout
    {
    public:
        ImGuiID render(bool reset = false)
        {
            auto* viewport = static_cast<ImGuiViewportP*>(ImGui::GetMainViewport());
            // Current-frame insets include the menu and any wrapped toolbar rows.
            const auto work = viewport->GetBuildWorkRect();
            const auto size = work.GetSize();
            const auto id = ImGui::GetID("EditorDockSpace");
            if (size.x < 1 || size.y < 1) {
                ImGui::DockSpace(id,ImVec2(0,0),ImGuiDockNodeFlags_KeepAliveOnly); return id;
            }
            auto* root = ImGui::DockBuilderGetNode(id);
            if (reset || corrupted(root)) {
                ImGui::DockBuilderRemoveNode(id); _migrated = false; root = nullptr;
            }
            if (!root) {
                ImGui::DockBuilderAddNode(id,ImGuiDockNodeFlags_DockSpace);
                ImGui::DockBuilderSetNodeSize(id,size);
                ImGuiID center = id;
                const auto left = ImGui::DockBuilderSplitNode(center,ImGuiDir_Left,.20f,nullptr,&center);
                const auto right = ImGui::DockBuilderSplitNode(center,ImGuiDir_Right,.28f,nullptr,&center);
                const auto bottom = ImGui::DockBuilderSplitNode(center,ImGuiDir_Down,.24f,nullptr,&center);
                ImGui::DockBuilderDockWindow("Scene Hierarchy",left);
                ImGui::DockBuilderDockWindow("inspector",right);
                ImGui::DockBuilderDockWindow("systems",bottom); ImGui::DockBuilderDockWindow("statistic",bottom);
                ImGui::DockBuilderDockWindow("Scene",center); ImGui::DockBuilderDockWindow("Game",center);
                ImGui::DockBuilderFinish(id);
                root = ImGui::DockBuilderGetNode(id);
            }
            // Older layouts predate Game. Dock an uninitialized/tiny Game window
            // without resetting valid user-created floating panels or splitters.
            if (!_migrated) {
                auto* game = ImGui::FindWindowSettingsByID(ImHashStr("Game"));
                if (!game || (!game->DockId && (game->Size.x < 160 || game->Size.y < 100))) {
                    if (auto* center = central(root)) { ImGui::DockBuilderDockWindow("Game",center->ID); _migrated = true; }
                } else {
                    _migrated = true;
                }
            }
            ImGui::SetNextWindowPos(work.Min); ImGui::SetNextWindowSize(size); ImGui::SetNextWindowViewport(viewport->ID);
            const auto flags = ImGuiWindowFlags_NoTitleBar|ImGuiWindowFlags_NoCollapse|ImGuiWindowFlags_NoResize|
                ImGuiWindowFlags_NoMove|ImGuiWindowFlags_NoDocking|ImGuiWindowFlags_NoBringToFrontOnFocus|
                ImGuiWindowFlags_NoNavFocus|ImGuiWindowFlags_NoSavedSettings;
            ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding,0);
            ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize,0);
            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,ImVec2(0,0));
            ImGui::Begin("##EditorDockHost",nullptr,flags); ImGui::PopStyleVar(3);
            ImGui::DockSpace(id,ImVec2(0,0)); ImGui::End();
            return id;
        }
    private:
        bool _migrated = false;
        static ImGuiDockNode* central(ImGuiDockNode* node)
        {
            if (node->IsCentralNode()) return node;
            for (auto* child : node->ChildNodes) if (child) if (auto* found = central(child)) return found;
            return nullptr;
        }
        static bool corrupted(ImGuiDockNode* root)
        {
            if (!root) return false;
            for (auto name : {"Scene Hierarchy","inspector","systems","statistic","Game"}) {
                const auto* window = ImGui::FindWindowByName(name);
                const auto* settings = ImGui::FindWindowSettingsByID(ImHashStr(name));
                const auto dock = window && window->DockId ? window->DockId : settings ? settings->DockId : 0;
                const auto* node = dock ? ImGui::DockBuilderGetNode(dock) : nullptr;
                if (node && node->IsLeafNode() && !node->IsCentralNode() &&
                    (node->SizeRef.x < 4 || node->SizeRef.y < 4)) return true;
            }
            return false;
        }
    };
}
