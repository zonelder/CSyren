#pragma once
#include "imGui/imgui.h"
#include "scene_session.h"
#include "editor_selection.h"
#include "core/services.h"
#include <optional>

// The bundled ImGui viewport sidebar reserves space below the main menu.
namespace ImGui
{
    IMGUI_API bool BeginViewportSideBar(const char*, ImGuiViewport*, ImGuiDir, float, ImGuiWindowFlags);
}

namespace csyren::editor
{
    class SceneWorkflowWindow
    {
    public:
        void shortcuts()
        {
            auto& session = *core::Services::get<SceneSession>();
            const auto& io = ImGui::GetIO();
            if (session.playing() || busy() || io.WantTextInput ||
                ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId)) return;
            if (!ImGui::IsAnyItemActive() && selectedKey(session))
            {
                if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_D, false)) entityAction(session, SceneOperation::DuplicateEntity);
                if (!io.KeyCtrl && !io.KeyAlt && ImGui::IsKeyPressed(ImGuiKey_Delete, false)) entityAction(session, SceneOperation::DeleteEntity);
            }
            if (!io.KeyCtrl) return;
            if (io.KeyShift && ImGui::IsKeyPressed(ImGuiKey_N, false))
            { session.finishEdit(); session.enqueue(SceneOperation::CreateEntity); }
            if (ImGui::IsKeyPressed(ImGuiKey_S, false)) save(session);
            if (ImGui::IsKeyPressed(ImGuiKey_Z, false) && (io.KeyShift ? session.canRedo() : session.canUndo()))
                session.enqueue(io.KeyShift ? SceneOperation::Redo : SceneOperation::Undo);
            if (ImGui::IsKeyPressed(ImGuiKey_Y, false) && session.canRedo()) session.enqueue(SceneOperation::Redo);
        }
        void menus()
        {
            auto& session = *core::Services::get<SceneSession>();
            const bool editable = !session.playing() && !busy();
            if (ImGui::BeginMenu("File"))
            {
                if (ImGui::MenuItem("Open / Replace...", nullptr, false, editable)) file(SceneOperation::Open);
                if (ImGui::MenuItem("Import...", nullptr, false, editable)) file(SceneOperation::Import);
                if (ImGui::MenuItem("Reload", nullptr, false, editable && !session.path().empty())) replace(session, SceneOperation::Reload);
                ImGui::Separator();
                if (ImGui::MenuItem("Save", "Ctrl+S", false, editable)) save(session);
                if (ImGui::MenuItem("Save As...", nullptr, false, editable)) file(SceneOperation::SaveAs);
                ImGui::EndMenu();
            }
            if (ImGui::BeginMenu("Edit"))
            {
                if (ImGui::MenuItem("Undo", "Ctrl+Z", false, editable && session.canUndo())) session.enqueue(SceneOperation::Undo);
                if (ImGui::MenuItem("Redo", "Ctrl+Y / Ctrl+Shift+Z", false, editable && session.canRedo())) session.enqueue(SceneOperation::Redo);
                ImGui::Separator();
                if (ImGui::MenuItem("Create empty object", "Ctrl+Shift+N", false, editable))
                { session.finishEdit(); session.enqueue(SceneOperation::CreateEntity); }
                if (ImGui::MenuItem("Duplicate object", "Ctrl+D", false, editable && selectedKey(session))) entityAction(session, SceneOperation::DuplicateEntity);
                if (ImGui::MenuItem("Delete object", "Delete", false, editable && selectedKey(session))) entityAction(session, SceneOperation::DeleteEntity);
                ImGui::EndMenu();
            }
        }
        void render()
        {
            auto& session = *core::Services::get<SceneSession>();
            const auto flags = ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse;
            const float height = ImGui::GetFrameHeight() + 2 * ImGui::GetStyle().WindowPadding.y;
            if (ImGui::BeginViewportSideBar("##SceneToolbar", ImGui::GetMainViewport(), ImGuiDir_Up, height, flags))
            {
                ImGui::BeginDisabled(session.playing() || busy());
                if (ImGui::Button("Play")) session.enqueue(SceneOperation::Play);
                ImGui::EndDisabled();
                ImGui::SameLine();
                ImGui::BeginDisabled(!session.playing() || busy());
                if (ImGui::Button("Stop")) session.enqueue(SceneOperation::Stop);
                ImGui::EndDisabled();
                ImGui::SameLine(); ImGui::TextDisabled("|"); ImGui::SameLine();
                ImGui::Text("%s | %s%s", session.playing() ? "Playing" : "Edit",
                    session.path().empty() ? "Untitled" : session.path().c_str(), session.dirty() ? " *" : "");
                if (!session.playing()) { ImGui::SameLine(); ImGui::TextDisabled("| Move: X/Y/Z handles | F: focus"); }
                if (!session.error().empty())
                {
                    ImGui::SameLine(); ImGui::TextColored(ImVec4(1, 0.4f, 0.4f, 1), "%s", session.error().c_str());
                }
            }
            ImGui::End();
            // Open and draw dialogs from the same stable ID scope, outside menus.
            if (_openFile) { ImGui::OpenPopup("Scene file"); _openFile = false; }
            if (_openUnsaved) { ImGui::OpenPopup("Unsaved scene"); _openUnsaved = false; }
            if (ImGui::BeginPopupModal("Scene file", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
            {
                const auto operation = *_fileOperation;
                ImGui::TextUnformatted(operation == SceneOperation::Open ? "Replace the current scene" :
                    operation == SceneOperation::Import ? "Import into the current scene" : "Save scene as");
                ImGui::SetNextItemWidth(480);
                ImGui::InputText("Path", _path, sizeof(_path));
                ImGui::TextDisabled("Relative to project assets, or an absolute path");
                ImGui::BeginDisabled(_path[0] == '\0');
                if (ImGui::Button("Confirm"))
                {
                    _fileOperation.reset();
                    if (operation == SceneOperation::Open) replace(session, operation);
                    else session.enqueue(operation, _path);
                    if (_saveBeforeReplace) { continueReplace(session); _saveBeforeReplace = false; }
                    ImGui::CloseCurrentPopup();
                }
                ImGui::EndDisabled(); ImGui::SameLine();
                if (ImGui::Button("Cancel"))
                {
                    _fileOperation.reset();
                    if (_saveBeforeReplace) { _pending.reset(); _saveBeforeReplace = false; }
                    ImGui::CloseCurrentPopup();
                }
                ImGui::EndPopup();
            }
            if (ImGui::BeginPopupModal("Unsaved scene", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
            {
                ImGui::TextUnformatted("Save changes before replacing the scene?");
                if (ImGui::Button("Save and continue"))
                {
                    if (session.path().empty()) { _saveBeforeReplace = true; file(SceneOperation::SaveAs); }
                    else { save(session); continueReplace(session); }
                    ImGui::CloseCurrentPopup();
                }
                ImGui::SameLine();
                if (ImGui::Button("Discard")) { continueReplace(session); ImGui::CloseCurrentPopup(); }
                ImGui::SameLine();
                if (ImGui::Button("Cancel")) { _pending.reset(); ImGui::CloseCurrentPopup(); }
                ImGui::EndPopup();
            }
        }
    private:
        static EntityKey selectedKey(SceneSession& session)
        {
            const auto selected = core::Services::get<EditorSelection>()->selected();
            return session.keyFor(selected);
        }
        static void entityAction(SceneSession& session, SceneOperation operation)
        {
            const auto key = selectedKey(session);
            if (!key) return;
            session.finishEdit();
            SceneRequest request{ operation, session.documentId() }; request.entity = key;
            session.enqueue(std::move(request));
        }
        bool busy() const { return _pending.has_value() || _fileOperation.has_value(); }
        void file(SceneOperation operation) { _fileOperation = operation; _openFile = true; }
        void save(SceneSession& session)
        {
            if (session.path().empty()) file(SceneOperation::SaveAs);
            else session.enqueue(SceneOperation::Save);
        }
        void continueReplace(SceneSession& session)
        {
            _pending->discardChanges = true;
            session.enqueue(std::move(*_pending)); _pending.reset();
        }
        void replace(SceneSession& session, SceneOperation operation)
        {
            session.finishEdit();
            SceneRequest request{ operation, session.documentId(), operation == SceneOperation::Open ? _path : session.path() };
            if (session.dirty()) { _pending = std::move(request); _openUnsaved = true; }
            else session.enqueue(std::move(request));
        }
        char _path[1024] = "test_scene.scene";
        std::optional<SceneRequest> _pending;
        std::optional<SceneOperation> _fileOperation;
        bool _openFile = false;
        bool _openUnsaved = false;
        bool _saveBeforeReplace = false;
    };
}
