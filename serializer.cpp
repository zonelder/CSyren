#include "serializer.h"
#include "core/scene.h"
#include "core/system_manager.h"
#include "core/meta_json.h"
#include "core/services.h"
#include "dx12_graphic/resource_handle_meta.h"
#include "dx12_graphic/resource_manager.h"
#include <fstream>
#include <filesystem>

namespace csyren
{

    bool Serializer::saveScene(const std::string& filepath, core::Scene& scene)
    {
        try {
            auto* manager = core::Services::get<render::ResourceManager>();
            if (!manager) return false;
            core::SerializationContext context;
            render::attachResources(context, *manager);
            nlohmann::json document;
            core::JsonArchiveWriter writer(document);
            if (!saveScene(writer, context, scene)) return false;
            std::ofstream file(std::filesystem::path("assets") / filepath);
            if (!file) return false;
            file << document.dump(4); file.close(); return file.good();
        } catch (const std::exception& error) {
            log::error("Failed to save scene '{}': {}", filepath, error.what()); return false;
        }
    }

    bool Serializer::loadScene(const std::string& filepath, core::Scene& scene)
    {
        try {
            std::ifstream file(std::filesystem::path("assets") / filepath);
            if (!file) return false;
            auto document = nlohmann::json::parse(file, nullptr, false);
            if (document.is_discarded()) return false;
            auto* manager = core::Services::get<render::ResourceManager>();
            if (!manager) return false;
            core::SerializationContext context;
            render::attachResources(context, *manager);
            core::JsonArchiveReader reader(document);
            return loadScene(reader, context, scene);
        } catch (const std::exception& error) {
            log::error("Failed to load scene '{}': {}", filepath, error.what()); return false;
        }
    }

    bool Serializer::loadSystems(const std::string& filepath, core::SystemManager& systems)
    {
        try {
            std::ifstream file(std::filesystem::path("assets") / filepath);
            if (!file) return false;
            const auto data = nlohmann::json::parse(file, nullptr, false);
            if (data.is_discarded() || !data.contains("systems") || !data["systems"].is_array()) return false;
            std::vector<std::string> names;
            for (const auto& entry : data["systems"]) names.push_back(entry.at("class").get<std::string>());
            systems.shutdown(); systems.reserve(names.size());
            for (const auto& name : names) systems.add(name);
            systems.init(); return true;
        } catch (const std::exception& error) {
            log::error("Failed to load systems '{}': {}", filepath, error.what()); return false;
        }
    }
    bool Serializer::saveSystems(const std::string& filepath, const core::SystemManager& systems)
    {
        try {
            nlohmann::json document; document["systems"] = nlohmann::json::array();
            for (const auto& system : systems.list()) document["systems"].push_back({ { "class", system.name } });
            std::ofstream file(std::filesystem::path("assets") / filepath);
            if (!file) return false;
            file << document.dump(4); file.close(); return file.good();
        } catch (const std::exception& error) {
            log::error("Failed to save systems '{}': {}", filepath, error.what()); return false;
        }
    }
}
