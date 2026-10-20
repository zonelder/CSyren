#pragma once
// Named ImGui draw callbacks that the editor runs inside its frame, after its
// own windows. Lets gameplay (or tools) draw overlays without touching the
// editor system. Callbacks must only use plain data they own: they run on the
// main thread inside the EditorSystem task, bound to the editor scene.
#include <functional>
#include <string>
#include <utility>
#include <vector>

namespace csyren::editor
{
    class EditorOverlays
    {
    public:
        using Draw = std::function<void()>;
        void set(const std::string& name, Draw draw)
        {
            for (auto& entry : _overlays) if (entry.first == name) { entry.second = std::move(draw); return; }
            _overlays.emplace_back(name, std::move(draw));
        }
        void remove(const std::string& name)
        {
            std::erase_if(_overlays, [&](const auto& entry) { return entry.first == name; });
        }
        void draw() const { for (const auto& entry : _overlays) if (entry.second) entry.second(); }
        bool empty() const noexcept { return _overlays.empty(); }
    private:
        std::vector<std::pair<std::string, Draw>> _overlays;
    };
}
