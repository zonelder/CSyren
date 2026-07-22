#pragma once
#include "base_editor_window.h"
#include "core/services.h"
#include "core/time.h"
#include "statistic_monitor.h"

namespace csyren::editor
{
    class StatisticWindow : public BaseEditorWindow
    {
    public:
        StatisticWindow() : BaseEditorWindow("statistic") {}

        void onFrame() override
        {
           auto stats =  core::Services::get<StatisticMonitor>();
           if (!stats) return;
           const float fps = stats->getCurrentFps();
           const float minFps = stats->getMinFpsLastMinute();
           const float maxFps = stats->getMaxFpsLastMinute();
           const uint32_t drawCalls = stats->getDrawCalls();

           ImVec4 fpsColor = fps >= 60.0f ? ImVec4(0.2f, 1.0f, 0.2f, 1.0f) :
               fps >= 30.0f ? ImVec4(1.0f, 1.0f, 0.2f, 1.0f) :
               ImVec4(1.0f, 0.2f, 0.2f, 1.0f);

           ImGui::TextColored(fpsColor, "FPS: %.1f", fps);
           ImGui::Text("Min (1m): %.1f | Max (1m): %.1f", minFps, maxFps);
           ImGui::Text("Draw Calls: %u", drawCalls);

           ImGui::Separator();
           const int historySize = static_cast<int>(stats->getHistorySize());
           const int historyOffset = static_cast<int>(stats->getHistoryOffset());


           float minMs = 0.0f;
           float maxMs = 16.6f;
           for (int i = 0; i < historySize; ++i)
           {
               float val = stats->getRawFrameTime(i);
               if (i == 0 || val < minMs) minMs = val;
               if (val > maxMs) maxMs = val;
           }

           maxMs = std::max(maxMs, 33.3f);

           ImGui::PlotLines(
               "##FrameTimeGraph",
               StatisticMonitorFrameTimeGetter,
               stats,
               historySize,
               historyOffset,
               "Frame Time (ms)",
               0.0f,
               maxMs,
               ImVec2(0, 60)
           );
        }
    };
}