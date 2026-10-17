#pragma once
#include "base_editor_window.h"
#include "core/services.h"
#include "core/system_manager.h"
#include <cmath>

namespace csyren::editor
{
    class SystemWindow : public BaseEditorWindow
    {
    public:
        SystemWindow() : BaseEditorWindow("systems") {}

        void onFrame() override
        {
            const auto* systems = core::Services::get<core::SystemManager>();
            if (ImGui::Combo("Schedule", &_graphMode, "Current\0Editor\0Game\0Play + Editor\0")) _selected.clear();
            const core::FrameGraph::Inspection* inspected = &systems->executionGraph();
            if (_graphMode != 0) {
                const auto& preview = systems->previewExecutionGraph(_graphMode != 1, _graphMode == 2);
                ImGui::TextDisabled("Preview only: systems and scenes are not switched or executed.");
                if (!preview.error.empty()) {
                    ImGui::TextWrapped("Cannot compile this schedule: %s", preview.error.c_str());
                    return;
                }
                inspected = &preview.graph;
            } else ImGui::TextDisabled("Current mode: %s", systems->playing() ? "Play + Editor" : "Editor");
            const auto& graph = *inspected;
            ImGui::Text("%s CPU graph: %zu tasks", _graphMode ? "Planned" : "Compiled", graph.tasks.size());
            ImGui::TextDisabled("Execution flows downward. Only immediate dependencies are shown.");
            const auto parents = graph.immediateParents();
            if (_graphMode == 0 && systems->executionGraphPending())
                ImGui::TextWrapped("Schedule changed: showing the previous compilation until the next execution.");
            if (ImGui::CollapsingHeader("Phases")) {
                for (const auto& phase : graph.phases) {
                    size_t count = 0;
                    for (const auto& node : graph.tasks) if (node.task.description.phase == phase.name) ++count;
                    ImGui::BulletText("%s (%zu tasks)", phase.name.c_str(), count);
                    for (const auto& name : phase.after) ImGui::Text("    after: %s", name.c_str());
                    for (const auto& name : phase.before) ImGui::Text("    before: %s", name.c_str());
                }
            }
            if (graph.phases.empty()) {
                ImGui::TextUnformatted("No compiled schedule yet.");
                return;
            }
            ImGui::SliderFloat("Zoom", &_zoom, 0.4f, 1.5f, "%.1fx");
            auto* font = ImGui::GetFont();
            // AddText otherwise rescales rounded baked glyphs by a fractional ratio.
            const float fontSize = font->GetFontBaked((std::max)(8.0f, ImGui::GetFontSize() * _zoom))->Size;
            float width = 220.0f * _zoom;
            for (const auto& node : graph.tasks)
                width = (std::max)(width, font->CalcTextSizeA(fontSize, FLT_MAX, 0, node.task.name.c_str()).x + 24.0f * _zoom);
            const float height = fontSize * 2.0f + 20.0f * _zoom;
            const float gap = 65.0f * _zoom, rowGap = 16.0f * _zoom;
            std::vector<size_t> depths(graph.tasks.size());
            size_t maxDepth = 0;
            for (size_t i = 0; i < graph.tasks.size(); ++i) {
                for (size_t parent : parents[i])
                    depths[i] = (std::max)(depths[i], depths[parent] + 1);
                maxDepth = (std::max)(maxDepth, depths[i]);
            }
            std::vector<ImVec2> positions(graph.tasks.size());
            struct Band { std::string name; float y, height; };
            std::vector<Band> bands;
            float y = 12.0f;
            float canvasWidth = width + 32.0f;
            for (const auto& phase : graph.phases) {
                const float start = y;
                y += 28.0f;
                size_t firstDepth = maxDepth, lastDepth = 0;
                bool populated = false;
                for (size_t i = 0; i < graph.tasks.size(); ++i) {
                    if (graph.tasks[i].task.description.phase != phase.name) continue;
                    firstDepth = (std::min)(firstDepth, depths[i]);
                    lastDepth = (std::max)(lastDepth, depths[i]);
                    populated = true;
                }
                std::vector<size_t> lanes(maxDepth + 1);
                for (size_t i = 0; i < graph.tasks.size(); ++i) {
                    if (graph.tasks[i].task.description.phase != phase.name) continue;
                    const size_t lane = lanes[depths[i]]++;
                    positions[i] = ImVec2(16.0f + lane * (width + rowGap), y + (depths[i] - firstDepth) * (height + gap));
                    canvasWidth = (std::max)(canvasWidth, positions[i].x + width + 16.0f);
                }
                y += populated ? (lastDepth - firstDepth) * (height + gap) + height + gap : 24.0f;
                bands.push_back({phase.name, start, y - start});
            }
            const float available = ImGui::GetContentRegionAvail().y;
            ImGui::BeginChild("ExecutionGraph", ImVec2(0, (std::max)(160.0f, (_selected.empty() ? available : available * 0.7f))), true,
                ImGuiWindowFlags_HorizontalScrollbar);
            const ImVec2 origin = ImGui::GetCursorScreenPos();
            const auto scale = ImGui::GetIO().DisplayFramebufferScale;
            auto snap = [&](ImVec2 p) { return ImVec2(std::round(p.x * scale.x) / scale.x, std::round(p.y * scale.y) / scale.y); };
            auto point = [&](ImVec2 p) { return snap(ImVec2(origin.x + p.x, origin.y + p.y)); };
            auto* draw = ImGui::GetWindowDrawList();
            const auto savedFlags = draw->Flags;
            // Positions are already snapped in framebuffer pixels. Do not let
            // ImGui truncate them again in logical coordinates at fractional DPI.
            draw->Flags |= ImDrawListFlags_TextNoPixelSnap;
            for (const auto& band : bands) {
                draw->AddRectFilled(point(ImVec2(0, band.y)), point(ImVec2(canvasWidth, band.y + band.height)),
                    IM_COL32(65, 75, 90, 45), 5.0f);
                // Keep phase boundaries distinct from the task dependency arrows.
                const auto separator = ImGui::GetColorU32(ImGuiCol_Separator);
                draw->AddLine(point(ImVec2(0, band.y)), point(ImVec2(canvasWidth, band.y)), separator);
                draw->AddLine(point(ImVec2(0, band.y + 3)), point(ImVec2(canvasWidth, band.y + 3)), separator);
                draw->AddText(point(ImVec2(8, band.y + 7)), ImGui::GetColorU32(ImGuiCol_Text), band.name.c_str());
            }
            for (size_t i = 0; i < graph.tasks.size(); ++i) {
                for (size_t parent : parents[i]) {
                    const ImVec2 a = point(ImVec2(positions[parent].x + width * 0.5f, positions[parent].y + height));
                    const ImVec2 b = point(ImVec2(positions[i].x + width * 0.5f, positions[i].y));
                    const bool selected = graph.tasks[i].task.name == _selected || graph.tasks[parent].task.name == _selected;
                    const ImU32 color = selected ? IM_COL32(255, 195, 80, 255) : IM_COL32(130, 150, 175, 110);
                    draw->AddBezierCubic(a, ImVec2(a.x, a.y + gap * 0.5f), ImVec2(b.x, b.y - gap * 0.5f), b, color, selected ? 2.0f : 1.0f);
                    draw->AddTriangleFilled(b, ImVec2(b.x - 4, b.y - 7), ImVec2(b.x + 4, b.y - 7), color);
                }
            }
            for (size_t i = 0; i < graph.tasks.size(); ++i) {
                const auto& task = graph.tasks[i].task;
                const bool commit = !task.description.structuralCommitFor.empty();
                const ImVec2 a = point(positions[i]), b(a.x + width, a.y + height);
                draw->AddRectFilled(a, b, commit ? IM_COL32(90, 65, 110, 255) : IM_COL32(45, 70, 95, 255), 5.0f);
                draw->AddRect(a, b, task.name == _selected ? IM_COL32(255, 195, 80, 255) : IM_COL32(110, 130, 150, 255), 5.0f);
                draw->PushClipRect(a, b, true);
                draw->AddText(font, fontSize, snap(ImVec2(a.x + 8 * _zoom, a.y + 6 * _zoom)), IM_COL32_WHITE, task.name.c_str());
                draw->AddText(font, fontSize, snap(ImVec2(a.x + 8 * _zoom, a.y + 10 * _zoom + fontSize)), IM_COL32(185, 200, 215, 255),
                    commit ? "Structural commit" : task.description.affinity == core::TaskAffinity::MainThread ? "Main thread" : "Worker");
                draw->PopClipRect();
                ImGui::SetCursorScreenPos(a);
                ImGui::PushID(static_cast<int>(i));
                if (ImGui::InvisibleButton("task", ImVec2(width, height))) _selected = task.name;
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s\nPhase: %s\n%zu immediate dependencies", task.name.c_str(), task.description.phase.c_str(), parents[i].size());
                ImGui::PopID();
            }
            ImGui::SetCursorScreenPos(origin);
            ImGui::Dummy(ImVec2(canvasWidth, y));
            draw->Flags = savedFlags;
            ImGui::EndChild();
            for (size_t i = 0; i < graph.tasks.size(); ++i) {
                const auto& node = graph.tasks[i];
                if (node.task.name != _selected) continue;
                const auto& d = node.task.description;
                ImGui::Text("%s | phase: %s", node.task.name.c_str(), d.phase.c_str());
                ImGui::BeginChild("TaskDetails", ImVec2(0, 0));
                ImGui::Text("Affinity: %s", d.affinity == core::TaskAffinity::MainThread ? "Main thread" : "Worker");
                if (d.exclusiveScene) ImGui::TextUnformatted("Exclusive scene access");
                if (d.dynamicResources) ImGui::TextUnformatted("Dynamic resources");
                if (!d.structuralCommitFor.empty()) ImGui::Text("Commit for: %s", d.structuralCommitFor.c_str());
                ImGui::TextUnformatted("Immediate predecessors:");
                if (parents[i].empty()) ImGui::TextUnformatted("  Root task");
                for (size_t parent : parents[i]) ImGui::BulletText("%s", graph.tasks[parent].task.name.c_str());
                for (const auto& access : d.accesses)
                    ImGui::BulletText("%s %s: %s [%s -> %s]", access.mode == core::AccessMode::Write ? "Write" : "Read",
                        access.component ? "component" : "resource", access.type.name(), access.requiredState.c_str(), access.resultingState.c_str());
                for (const auto& access : d.gpuAccesses)
                    ImGui::BulletText("GPU %s: %s | queue: %s | state: %u", access.mode == core::AccessMode::Write ? "Write" : "Read",
                        access.resource.c_str(), d.gpuQueue.c_str(), access.requiredState);
                ImGui::EndChild();
            }
        }
    private:
        int _graphMode = 0;
        float _zoom = 1.0f;
        std::string _selected;
    };
}
