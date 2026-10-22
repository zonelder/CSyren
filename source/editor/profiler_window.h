#pragma once
// Profiler window: timeline of one frame (a track per thread, nested marks),
// per-mark metrics and thread load. Data comes from profiler::Collector; the
// editor UI is drawn in the middle of a frame, so it shows the newest closed
// frame. Pause freezes the frames kept so far to inspect one of them.
#include "base_editor_window.h"
#include "cstdmf/profiler.h"
#include "core/project_paths.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <unordered_map>
#include <vector>

namespace csyren::editor
{
    class ProfilerWindow : public BaseEditorWindow
    {
    public:
        ProfilerWindow() : BaseEditorWindow("profiler")
        {
            std::snprintf(_capturePath, sizeof(_capturePath), "%s",
                core::pathToUtf8(core::projectBuildPath("profiler_capture.json")).c_str());
        }

        void onFrame() override
        {
            auto& collector = profiler::Collector::instance();
            toolbar(collector);
            const auto frames = closedFrames(collector);
            if (frames.empty()) { ImGui::TextDisabled("No frames yet."); return; }
            if (_selected >= frames.size()) _selected = frames.size() - 1;
            const auto& frame = *frames[_paused ? _selected : frames.size() - 1];
            frameGraph(frames, frame);
            if (ImGui::CollapsingHeader("Timeline", ImGuiTreeNodeFlags_DefaultOpen)) timeline(collector, frame);
            if (ImGui::CollapsingHeader("Marks", ImGuiTreeNodeFlags_DefaultOpen)) marks(collector);
            if (ImGui::CollapsingHeader("Threads")) threads(collector);
        }

    private:
        void toolbar(profiler::Collector& collector)
        {
            bool enabled = profiler::enabled();
            if (ImGui::Checkbox("Enabled", &enabled)) profiler::setEnabled(enabled);
            ImGui::SameLine();
            if (ImGui::Button(_paused ? "Resume" : "Pause")) {
                _paused = !_paused;
                if (_paused) freeze(collector);
                else { _frozen.clear(); _frozenStats.clear(); _frozenThreads.clear(); }
            }
            ImGui::SameLine();
            if (ImGui::Button("Save capture")) {
                const auto path = core::pathFromUtf8(_capturePath);
                std::error_code error;
                std::filesystem::create_directories(path.parent_path(), error);
                // Paused: the frozen frames, so the frame being inspected is in the file.
                std::vector<const profiler::Frame*> frozen;
                for (const auto& frame : _frozen) frozen.push_back(&frame);
                const bool saved = _paused ? collector.exportChromeTrace(path, frozen) : collector.exportChromeTrace(path);
                _saveStatus = saved ? "Saved (open in ui.perfetto.dev)" : "Save failed";
            }
            ImGui::SameLine();
            ImGui::SetNextItemWidth((std::max)(120.0f, ImGui::GetContentRegionAvail().x - 4));
            ImGui::InputText("##capture", _capturePath, sizeof(_capturePath));
            if (!_saveStatus.empty()) ImGui::TextDisabled("%s", _saveStatus.c_str());
            if (collector.dropped() || collector.late())
                ImGui::TextColored(ImVec4(1, .7f, .2f, 1), "Dropped marks: %llu (full thread buffer), outside kept frames: %llu",
                    static_cast<unsigned long long>(collector.dropped()), static_cast<unsigned long long>(collector.late()));
        }

        void freeze(const profiler::Collector& collector)
        {
            _frozen.clear();
            for (const auto& frame : collector.frames()) if (frame.end) _frozen.push_back(frame);
            _frozenStats = collector.stats();
            _frozenThreads = collector.threads();
            _selected = _frozen.empty() ? 0 : _frozen.size() - 1;
        }

        std::vector<const profiler::Frame*> closedFrames(const profiler::Collector& collector) const
        {
            std::vector<const profiler::Frame*> result;
            if (_paused) for (const auto& frame : _frozen) result.push_back(&frame);
            else for (const auto& frame : collector.frames()) if (frame.end) result.push_back(&frame);
            return result;
        }

        void frameGraph(const std::vector<const profiler::Frame*>& frames, const profiler::Frame& shown)
        {
            _times.resize(frames.size());
            float maxMs = 16.7f;
            size_t slowest = 0;
            for (size_t i = 0; i < frames.size(); ++i) {
                _times[i] = float(frames[i]->ms());
                if (_times[i] > maxMs) maxMs = _times[i];
                if (_times[i] > _times[slowest]) slowest = i;
            }
            char overlay[96];
            std::snprintf(overlay, sizeof(overlay), "frame %llu: %.2f ms", static_cast<unsigned long long>(shown.index), shown.ms());
            ImGui::PlotHistogram("##frames", _times.data(), int(_times.size()), 0, overlay, 0.0f, maxMs * 1.1f, ImVec2(-1, 50));
            if (_paused) {
                int selected = int(_selected);
                ImGui::SetNextItemWidth(-110);
                if (ImGui::SliderInt("##frame", &selected, 0, int(frames.size()) - 1, "frame %d")) _selected = size_t(selected);
                ImGui::SameLine();
                if (ImGui::Button("Slowest")) _selected = slowest;
            } else ImGui::TextDisabled("Pause to pick a frame.");
        }

        const std::string& markName(const profiler::Collector& collector, uint32_t id)
        {
            auto [it, inserted] = _names.try_emplace(id);
            if (inserted) it->second = collector.nameOf(id);
            return it->second;
        }

        void timeline(const profiler::Collector& collector, const profiler::Frame& frame)
        {
            constexpr float row = 18.0f, label = 120.0f, gap = 6.0f;
            // Tracks: threads with marks in this frame, ordered by index.
            std::vector<std::pair<uint16_t, uint16_t>> tracks; // thread, depth count
            for (const auto& span : frame.spans) {
                auto it = std::find_if(tracks.begin(), tracks.end(), [&](const auto& t) { return t.first == span.thread; });
                if (it == tracks.end()) tracks.push_back({ span.thread, uint16_t(span.depth + 1) });
                else it->second = (std::max)(it->second, uint16_t(span.depth + 1));
            }
            std::sort(tracks.begin(), tracks.end());
            float height = 0;
            for (const auto& track : tracks) height += track.second * row + gap;
            ImGui::TextDisabled("Ctrl+wheel: zoom. Hover a mark for details.");
            ImGui::BeginChild("##timeline", ImVec2(0, (std::min)(height + 24.0f, 320.0f)), ImGuiChildFlags_Borders,
                ImGuiWindowFlags_HorizontalScrollbar);
            const auto& io = ImGui::GetIO();
            if (ImGui::IsWindowHovered() && io.KeyCtrl && io.MouseWheel != 0.0f)
                _zoom = std::clamp(_zoom * (io.MouseWheel > 0 ? 1.25f : 0.8f), 1.0f, 200.0f);
            const float width = (std::max)(ImGui::GetContentRegionAvail().x - label, 50.0f) * _zoom;
            ImGui::Dummy(ImVec2(width + label, height));
            const ImVec2 origin = ImGui::GetItemRectMin();
            auto* draw = ImGui::GetWindowDrawList();
            const double duration = double((std::max<profiler::Ticks>)(frame.end - frame.start, 1));
            const float x0 = origin.x + label;
            const float scroll = ImGui::GetScrollX();
            const ImVec2 mouse = io.MousePos;
            const profiler::Span* hovered = nullptr;
            // Marks outside the visible part (zoomed in) are skipped.
            const float visibleLeft = draw->GetClipRectMin().x, visibleRight = draw->GetClipRectMax().x;
            float y = origin.y;
            for (const auto& [thread, depths] : tracks) {
                const float trackHeight = depths * row;
                draw->AddRectFilled(ImVec2(x0, y), ImVec2(x0 + width, y + trackHeight), IM_COL32(255, 255, 255, 10));
                for (const auto& span : frame.spans) {
                    if (span.thread != thread) continue;
                    const float a = x0 + float(double(span.start - frame.start) / duration * width);
                    const float b = (std::max)(a + 1.0f, x0 + float(double(span.end - frame.start) / duration * width));
                    if (b < visibleLeft || a > visibleRight) continue;
                    const float top = y + span.depth * row;
                    const ImVec2 min(a, top + 1), max(b, top + row - 1);
                    draw->AddRectFilled(min, max, color(span.name));
                    if (b - a > 24.0f) {
                        const ImVec4 clip(min.x, min.y, max.x - 2, max.y);
                        draw->AddText(nullptr, 0.0f, ImVec2(a + 3, top + 2), IM_COL32(15, 15, 15, 255),
                            markName(collector, span.name).c_str(), nullptr, 0.0f, &clip);
                    }
                    if (mouse.x >= min.x && mouse.x < max.x && mouse.y >= min.y && mouse.y < max.y && ImGui::IsWindowHovered())
                        hovered = &span;
                }
                // Track label, kept visible while scrolling.
                const float lx = origin.x + scroll;
                draw->AddRectFilled(ImVec2(lx, y), ImVec2(lx + label - 4, y + trackHeight), ImGui::GetColorU32(ImGuiCol_WindowBg));
                const auto name = collector.threadName(thread);
                draw->AddText(ImVec2(lx + 4, y + 2), ImGui::GetColorU32(ImGuiCol_Text), name.empty() ? "?" : name.c_str());
                y += trackHeight + gap;
            }
            ImGui::EndChild();
            if (hovered) {
                ImGui::BeginTooltip();
                ImGui::TextUnformatted(markName(collector, hovered->name).c_str());
                ImGui::Text("%.3f ms on %s", double(hovered->end - hovered->start) * 1e-6, collector.threadName(hovered->thread).c_str());
                ImGui::TextDisabled("starts at +%.3f ms, depth %u", double(hovered->start - frame.start) * 1e-6, unsigned(hovered->depth));
                ImGui::EndTooltip();
            }
        }

        void marks(profiler::Collector& collector)
        {
            const auto& stats = _paused ? _frozenStats : collector.stats(); // cached by the collector
            _filter.Draw("Filter", 180.0f);
            ImGui::SameLine();
            ImGui::TextDisabled("over the last %zu frames", profiler::Collector::StatFrames);
            std::vector<const profiler::Stat*> timed, values;
            for (const auto& stat : stats) {
                if (!_filter.PassFilter(markName(collector, stat.name).c_str())) continue;
                (stat.isValue ? values : timed).push_back(&stat);
            }
            const auto flags = ImGuiTableFlags_Sortable | ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV |
                ImGuiTableFlags_Resizable | ImGuiTableFlags_ScrollY | ImGuiTableFlags_SizingStretchProp;
            if (ImGui::BeginTable("##marks", 5, flags, ImVec2(0, 260))) {
                ImGui::TableSetupScrollFreeze(0, 1);
                ImGui::TableSetupColumn("Mark", ImGuiTableColumnFlags_WidthStretch, 3.0f);
                ImGui::TableSetupColumn("ms / frame", ImGuiTableColumnFlags_DefaultSort | ImGuiTableColumnFlags_PreferSortDescending, 1.0f);
                ImGui::TableSetupColumn("max ms", ImGuiTableColumnFlags_PreferSortDescending, 1.0f);
                ImGui::TableSetupColumn("calls / frame", ImGuiTableColumnFlags_PreferSortDescending, 1.0f);
                ImGui::TableSetupColumn("threads", ImGuiTableColumnFlags_NoSort, 1.5f);
                ImGui::TableHeadersRow();
                if (auto* sort = ImGui::TableGetSortSpecs(); sort && sort->SpecsCount > 0) {
                    const auto column = sort->Specs[0].ColumnIndex;
                    const bool ascending = sort->Specs[0].SortDirection == ImGuiSortDirection_Ascending;
                    std::sort(timed.begin(), timed.end(), [&](const profiler::Stat* a, const profiler::Stat* b) {
                        auto key = [&](const profiler::Stat* s) -> double {
                            return column == 1 ? s->msPerFrame : column == 2 ? s->maxMs : column == 3 ? s->callsPerFrame : 0.0; };
                        if (column == 0) {
                            const auto& na = markName(collector, a->name); const auto& nb = markName(collector, b->name);
                            return ascending ? na < nb : nb < na;
                        }
                        return ascending ? key(a) < key(b) : key(b) < key(a);
                    });
                }
                ImGuiListClipper clipper; // only the visible rows are built
                clipper.Begin(int(timed.size()));
                while (clipper.Step()) for (int row = clipper.DisplayStart; row < clipper.DisplayEnd; ++row) {
                    const auto* stat = timed[size_t(row)];
                    ImGui::TableNextRow();
                    ImGui::TableNextColumn(); ImGui::TextUnformatted(markName(collector, stat->name).c_str());
                    ImGui::TableNextColumn(); ImGui::Text("%.3f", stat->msPerFrame);
                    ImGui::TableNextColumn(); ImGui::Text("%.3f", stat->maxMs);
                    ImGui::TableNextColumn(); ImGui::Text("%.2f", stat->callsPerFrame);
                    ImGui::TableNextColumn(); ImGui::TextUnformatted(threadList(collector, stat->threads).c_str());
                }
                ImGui::EndTable();
            }
            if (!values.empty() && ImGui::TreeNodeEx("Values", ImGuiTreeNodeFlags_DefaultOpen)) {
                for (const auto* stat : values)
                    ImGui::Text("%s: %.4g (avg %.4g)", markName(collector, stat->name).c_str(), stat->lastValue, stat->avgValue);
                ImGui::TreePop();
            }
        }

        void threads(const profiler::Collector& collector)
        {
            const auto list = _paused ? _frozenThreads : collector.threads();
            for (const auto& thread : list) {
                ImGui::Text("%-12s", thread.name.c_str());
                ImGui::SameLine(130);
                char overlay[32];
                std::snprintf(overlay, sizeof(overlay), "%.0f%%", thread.busy * 100.0);
                ImGui::ProgressBar(float(std::clamp(thread.busy, 0.0, 1.0)), ImVec2(-1, 0), overlay);
            }
        }

        static std::string threadList(const profiler::Collector& collector, const std::vector<uint16_t>& threads)
        {
            std::string text;
            for (const auto thread : threads) {
                if (!text.empty()) text += ", ";
                text += collector.threadName(thread);
            }
            return text;
        }
        static ImU32 color(uint32_t id)
        {
            const float hue = std::fmod(float(id) * 0.61803398875f, 1.0f);
            return ImColor::HSV(hue, 0.45f, 0.90f);
        }

        bool _paused = false;
        size_t _selected = 0;
        float _zoom = 1.0f;
        std::vector<profiler::Frame> _frozen;
        std::vector<profiler::Stat> _frozenStats;
        std::vector<profiler::ThreadInfo> _frozenThreads;
        std::vector<float> _times;
        std::unordered_map<uint32_t, std::string> _names;
        ImGuiTextFilter _filter;
        char _capturePath[512]{};
        std::string _saveStatus;
    };
}
