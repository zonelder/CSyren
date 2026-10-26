#pragma once
#include "core/project_paths.h"
#include "third_party/json/json.hpp"
#include <fstream>
#include <stdexcept>

namespace csyren
{
    struct ProjectSettings
    {
        std::string name = "CSyren project";
        std::string projectPath = ".";
        std::string assetsPath = "assets";
        std::string appSystems = "assets/app.systems";
        std::string editorSystems = "assets/editor.systems";
        std::string startScene;
        std::filesystem::path file;
        // The file as it was read. save() writes the fields above into it, so keys the engine
        // does not use (the engine id CSyrenHub keeps in "engine") survive a save.
        nlohmann::json document = nlohmann::json::object();

        core::ProjectPaths paths() const
        {
            const auto root = (file.parent_path() / core::pathFromUtf8(projectPath)).lexically_normal();
            return { root, (root / core::pathFromUtf8(assetsPath)).lexically_normal() };
        }
        static ProjectSettings load(const std::filesystem::path& path)
        {
            ProjectSettings settings;
            settings.file = std::filesystem::absolute(path).lexically_normal();
            std::ifstream input(settings.file);
            if (!input) throw std::runtime_error("Cannot open project: " + core::pathToUtf8(settings.file));
            const auto json = nlohmann::json::parse(input);
            if (!json.is_object() || json.at("version").get<int>() != 1)
                throw std::runtime_error("Unsupported .csyrenproj version");
            settings.document = json;
            settings.name = json.value("name", settings.name);
            settings.projectPath = json.value("projectPath", settings.projectPath);
            settings.assetsPath = json.value("assetsPath", settings.assetsPath);
            settings.appSystems = json.at("appSystems").get<std::string>();
            settings.editorSystems = json.at("editorSystems").get<std::string>();
            settings.startScene = json.value("startScene", std::string{});
            if (settings.projectPath.empty() || settings.assetsPath.empty() ||
                settings.appSystems.empty() || settings.editorSystems.empty())
                throw std::runtime_error("Project paths must not be empty");
            const auto paths = settings.paths();
            if (!std::filesystem::is_directory(paths.root))
                throw std::runtime_error("Project root does not exist: " + core::pathToUtf8(paths.root));
            if (!std::filesystem::is_directory(paths.assets))
                throw std::runtime_error("Project assets directory does not exist: " + core::pathToUtf8(paths.assets));
            for (const auto& value : { settings.appSystems, settings.editorSystems, settings.startScene })
                if (!value.empty() && !std::filesystem::is_regular_file(paths.resolve(core::pathFromUtf8(value))))
                    throw std::runtime_error("Project file does not exist: " + core::pathToUtf8(paths.resolve(core::pathFromUtf8(value))));
            return settings;
        }
        bool save() const
        {
            nlohmann::json json = document.is_object() ? document : nlohmann::json::object();
            json["version"] = 1; json["name"] = name; json["projectPath"] = projectPath;
            json["assetsPath"] = assetsPath; json["appSystems"] = appSystems;
            json["editorSystems"] = editorSystems; json["startScene"] = startScene;
            std::ofstream output(file);
            if (!output) return false;
            output << json.dump(4); output.close(); return output.good();
        }
    };
}
