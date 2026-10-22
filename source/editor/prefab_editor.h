#pragma once
// Inspector editor for core::PrefabRef fields: the asset path and whether the
// file exists (an empty reference means "nothing to spawn").
#include "editor_base.h"
#include "core/prefab.h"
#include "core/project_paths.h"
#include "prefab_file.h"
#include <algorithm>
#include <chrono>
#include <filesystem>
#include <string>
#include <unordered_map>

namespace csyren::editor
{
    class PrefabRefEditor : public IEditor
    {
    public:
        void draw(core::reflection::MetaAny& any, const char* label, ValueEditContext& context) override
        {
            auto& value = any.get<core::PrefabRef>();
            context.draw(value, [&] {
                char buffer[512]{};
                const auto length = (std::min)(value.path.size(), sizeof(buffer) - 1);
                std::copy_n(value.path.data(), length, buffer);
                if (!ImGui::InputText(label, buffer, sizeof(buffer))) return false;
                value.path = buffer;
                return true;
            });
            if (value.empty()) ImGui::TextDisabled("  not set: nothing is spawned");
            else if (!exists(value.path)) ImGui::TextColored(ImVec4(1.0f, 0.45f, 0.35f, 1.0f), "  prefab file not found");
        }
    private:
        // File checks are cached for a second (the inspector draws every frame).
        bool exists(const std::string& path)
        {
            const auto now = std::chrono::steady_clock::now();
            auto& entry = _checked[path];
            if (now - entry.time > std::chrono::seconds(1)) {
                std::error_code error;
                entry.exists = std::filesystem::is_regular_file(core::projectAssetPath(core::pathFromUtf8(core::normalizePrefabPath(path))), error);
                entry.time = now;
            }
            return entry.exists;
        }
        struct Check { bool exists = false; std::chrono::steady_clock::time_point time{}; };
        std::unordered_map<std::string, Check> _checked;
    };
}
