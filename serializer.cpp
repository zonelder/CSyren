#include "serializer.h"
#include "core/scene.h"
#include "core/system_manager.h"
#include "core/meta_json.h"
#include "core/services.h"
#include "core/project_paths.h"
#include "cstdmf/file_system.h"
#include "dx12_graphic/resource_handle_meta.h"
#include "dx12_graphic/resource_manager.h"
#include "cstdmf/window_clean.h"
#include <atomic>
#include <fstream>
#include <filesystem>
#include <stdexcept>

namespace csyren
{
    namespace
    {
        void attachResourceContext(core::SerializationContext& context)
        {
            if (core::Services::has<render::ResourceManager>())
                render::attachResources(context, *core::Services::get<render::ResourceManager>());
        }
        std::filesystem::path scenePath(const std::string& utf8, bool writing = false)
        {
            const auto path = core::pathFromUtf8(utf8);
            auto& files = cstdmf::FileSystem::instance();
            const auto colon = utf8.find(":/");
            const bool mounted = colon != std::string::npos && colon > 1;
            if (mounted || (!core::Services::has<core::ProjectPaths>() && !path.is_absolute() && !files.mounts().empty())) {
                auto resolved = writing ? files.resolveWrite(path) : files.resolveRead(path);
                if (resolved) return *resolved;
                throw std::runtime_error("Cannot resolve scene or systems path: " + utf8);
            }
            return core::projectAssetPath(path);
        }
    }
    void ArchiveSceneStorage::configureComponentArchive(core::SerializationContext& context)
    { attachResourceContext(context); }

    bool Serializer::snapshot(core::Scene& scene, std::string& result)
    {
        try {
            core::SerializationContext context;
            attachResourceContext(context);
            nlohmann::json document;
            core::JsonArchiveWriter writer(document);
            if (!saveScene(writer, context, scene)) return false;
            result = document.dump(4); return true;
        } catch (const std::exception& error) {
            log::error("Failed to snapshot scene: {}", error.what()); return false;
        }
    }
    bool Serializer::restore(const std::string& snapshot, core::Scene& scene)
    {
        try {
            auto document = nlohmann::json::parse(snapshot, nullptr, false);
            if (document.is_discarded()) return false;
            core::SerializationContext context;
            attachResourceContext(context);
            core::JsonArchiveReader reader(document);
            return loadScene(reader, context, scene);
        } catch (const std::exception& error) {
            log::error("Failed to restore scene: {}", error.what()); return false;
        }
    }
    bool Serializer::saveScene(const std::string& filepath, core::Scene& scene)
    {
        try {
            std::string document;
            if (filepath.empty() || !snapshot(scene, document)) return false;
            const auto target = scenePath(filepath, true);
            std::filesystem::create_directories(target.parent_path());
            static std::atomic<uint64_t> nextTemp{};
            auto temporary = target;
            temporary += ".tmp." + std::to_string(GetCurrentProcessId()) + "." + std::to_string(++nextTemp);
            struct Cleanup {
                std::filesystem::path path;
                ~Cleanup() { std::error_code ignored; std::filesystem::remove(path, ignored); }
            } cleanup{ temporary };
            std::ofstream file(temporary, std::ios::binary | std::ios::trunc);
            if (!file) return false;
            file.write(document.data(), static_cast<std::streamsize>(document.size()));
            file.close();
            if (!file.good()) return false;
            // Same-directory rename replaces the destination only after complete writing.
            return MoveFileExW(temporary.c_str(), target.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0;
        } catch (const std::exception& error) {
            log::error("Failed to save scene '{}': {}", filepath, error.what()); return false;
        }
    }
    bool Serializer::loadScene(const std::string& filepath, core::Scene& scene)
    {
        try {
            std::ifstream file(scenePath(filepath), std::ios::binary);
            if (!file) return false;
            std::string document{ std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>() };
            return !file.bad() && restore(document, scene);
        } catch (const std::exception& error) {
            log::error("Failed to load scene '{}': {}", filepath, error.what()); return false;
        }
    }
    bool Serializer::loadSystems(const std::string& filepath, core::SystemManager& systems)
    {
        return loadSystems(filepath, "", systems);
    }
    bool Serializer::loadSystems(const std::string& appPath, const std::string& editorPath, core::SystemManager& systems)
    {
        try {
            std::vector<core::PhaseDescription> phases;
            auto read = [&phases](const std::string& path) {
                std::ifstream file(scenePath(path));
                if (!file) throw std::runtime_error("Cannot open systems configuration: " + path);
                auto data = nlohmann::json::parse(file);
                if (!data.contains("systems") || !data["systems"].is_array())
                    throw std::runtime_error("Expected a systems array: " + path);
                if (data.contains("phases")) for (const auto& phase : data["phases"]) {
                    core::PhaseDescription description;
                    phase.at("name").get_to(description.name);
                    description.after = phase.value("after", std::vector<std::string>{});
                    description.before = phase.value("before", std::vector<std::string>{});
                    if (std::none_of(phases.begin(), phases.end(), [&](const auto& p) { return p.name == description.name; })) phases.push_back(std::move(description));
                }
                return data["systems"];
            };
            core::SystemConfiguration configuration;
            auto validate = [](const std::string& name) {
                if (!core::details::SystemRegistry::instance().contains(name))
                    throw std::runtime_error("Unknown system: " + name);
            };
            for (const auto& entry : read(appPath)) {
                const auto name = entry.at("class").get<std::string>(); validate(name);
                if (std::find(configuration.app.begin(), configuration.app.end(), name) != configuration.app.end())
                    throw std::runtime_error("Duplicate application system: " + name);
                configuration.app.push_back(name);
            }
            if (!editorPath.empty()) for (const auto& entry : read(editorPath)) {
                const auto name = entry.at("class").get<std::string>(); validate(name);
                const auto scene = entry.value("scene", std::string("editor"));
                if (scene != "editor" && scene != "active") throw std::runtime_error("Unknown scene binding: " + scene);
                for (const auto& existing : configuration.editor)
                    if (existing.name == name) throw std::runtime_error("Duplicate editor system: " + name);
                if (scene == "editor" && std::find(configuration.app.begin(), configuration.app.end(), name) != configuration.app.end())
                    throw std::runtime_error("Application system cannot be pinned to the editor scene: " + name);
                configuration.editor.push_back({ name, scene == "editor" ? core::EditorSystemScene::Editor : core::EditorSystemScene::Active });
            }
            // Validate both files before changing the running configuration.
            systems.shutdown(); systems.reserve(configuration.app.size() + configuration.editor.size());
            for (auto& phase : phases) systems.phase(std::move(phase));
            for (const auto& name : configuration.app) systems.add(name);
            for (const auto& entry : configuration.editor) systems.addEditor(entry.name, entry.scene);
            systems.init(); return true;
        } catch (const std::exception& error) {
            log::error("Failed to load systems '{}', '{}': {}", appPath, editorPath, error.what()); return false;
        }
    }
    bool Serializer::saveSystems(const std::string& filepath, const core::SystemManager& systems)
    {
        return saveSystems(filepath, "", systems);
    }
    bool Serializer::saveSystems(const std::string& appPath, const std::string& editorPath, const core::SystemManager& systems)
    {
        try {
            auto write = [](const std::string& path, const nlohmann::json& document) {
                std::ofstream file(scenePath(path, true));
                if (!file) return false;
                file << document.dump(4); file.close(); return file.good();
            };
            nlohmann::json app, editor;
            app["systems"] = nlohmann::json::array(); editor["systems"] = nlohmann::json::array();
            app["phases"] = nlohmann::json::array();
            for (const auto& phase : systems.phases()) app["phases"].push_back({{"name", phase.name}, {"after", phase.after}, {"before", phase.before}});
            for (const auto& name : systems.configuration().app) app["systems"].push_back({ { "class", name } });
            for (const auto& entry : systems.configuration().editor)
                editor["systems"].push_back({ { "class", entry.name },
                    { "scene", entry.scene == core::EditorSystemScene::Editor ? "editor" : "active" } });
            return write(appPath, app) && (editorPath.empty() || write(editorPath, editor));
        } catch (const std::exception& error) {
            log::error("Failed to save systems '{}', '{}': {}", appPath, editorPath, error.what()); return false;
        }
    }
}
