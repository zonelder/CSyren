#include "serializer.h"

#include "core/scene.h"
#include "core/system_manager.h"
#include "core/serialize_base.h"
#include "dx12_graphic/serialize_common.h"

#include <fstream>

namespace csyren
{
   
    bool Serializer::saveScene(const std::string& filepath, core::Scene& scene)
    {
        json sceneJson;
        json entitiesArray = json::array();

        auto& serv = render::SerializationServices::get();
        serv.rootJson = &sceneJson;
        for (auto entt : scene.entities())
        {
            json entityJson;
            entityJson["id"] = entt.id;

            json componentsJson;

            for (const auto& [family, info] : core::reflection::ComponentRegistry::getAll())
            {
                if (info.has(&scene, entt.id))
                {
                    json componentJson;
                    if(info.serialize)
                        info.serialize(info.getRaw(&scene, entt.id), componentJson);
                    componentsJson[info.name] = componentJson;
                }
            }
            entityJson["components"] = componentsJson;
            entityJson["name"] = entt.name;
            entitiesArray.push_back(entityJson);

        }
        sceneJson["entities"] = entitiesArray;

        try
        {
            std::string sourcePath = (std::filesystem::path("assets") / filepath).string();
            std::ofstream o(sourcePath);
            o << sceneJson.dump(4);
            o.close();
            return o.good();
        }
        catch (const std::exception& e)
        {
            log::error("Failed to save scene to '{}': {}", filepath, e.what());
            return false;
        }

    }

    bool Serializer::loadSystems(const std::string& filepath, core::SystemManager& systems)
    {
        std::string sourcePath = (std::filesystem::path("assets") / filepath).string();
        std::ifstream f(sourcePath);

        if (!f.is_open())
        {
            log::error("Serializer::loadSystems : Failed to open system file {}", filepath);
            return false;
        }

        json data;

        try
        {
            data = json::parse(f, nullptr, false);
        }
        catch (const std::exception& e)
        {
            log::error("Serializer::loadSystems : Failed to parse systems file '{}': {}", filepath, e.what());
            return false;
        }

        if (data.is_discarded())
        {
            log::error("Serializer::loadSystems : Systems file '{}' is malformed.", filepath);
            return false;
        }
        if (!data.contains("systems")) return false;

        systems.shutdown();
        const auto& sysJsons = data["systems"];
        systems.reserve(sysJsons.size());
        for (const auto& system : sysJsons)
        {
            std::string name;
            system["class"].get_to(name);
            systems.add(name);
        }
        systems.init();

        return true;
    }

    bool Serializer::saveSystems(const std::string& filepath,const core::SystemManager& systems)
    {
        json configJson;
        json entitiesArray = json::array();

        for (const auto& sys : systems.list())
        {
            json sysJson;
            sysJson["class"] = sys.name;
            entitiesArray.push_back(sysJson);

        }
        configJson["systems"] = entitiesArray;

        try
        {
            std::string sourcePath = (std::filesystem::path("assets") / filepath).string();
            std::ofstream o(sourcePath);
            o << configJson.dump(4);
            o.close();
            return o.good();
        }
        catch (const std::exception& e)
        {
            log::error("Failed to save systems to '{}': {}", filepath, e.what());
            return false;
        }
    }
    bool Serializer::loadScene(const std::string& filepath,core::Scene& scene)
    {
        std::string sourcePath = (std::filesystem::path("assets") / filepath).string();
        std::ifstream f(sourcePath);

        if (!f.is_open())
        {
            log::error("Serializer::loadScene : Failed to open scene file {}", filepath);
            return false;
        }

        json data;

        try
        {
            data = json::parse(f, nullptr, false);
        }
        catch (const std::exception& e)
        {
            log::error("Serializer::loadScene : Failed to parse scene file '{}': {}", filepath, e.what());
            return false;
        }

        if (data.is_discarded())
        {
            log::error("Serializer::loadScene : Scene file '{}' is malformed.", filepath);
            return false;
        }

        if (!data.contains("entities")) return true;

        auto& serv = render::SerializationServices::get();
        serv.rootJson = &data;

        for (const auto& entityData : data["entities"])
        {
            std::string name;
            entityData["name"].get_to(name);
            core::Entity::ID newEntity = scene.createEntity(name);

            if (!entityData.contains("components")) continue;

            for (const auto& [name, componentData] : entityData["components"].items())
            {
                const auto info = core::reflection::ComponentRegistry::get(name);

                if (!info)
                {
                    log::warning("Serializer::loadScene : Skipping unknown component \'{}\' while loading scene {}", name, filepath);
                    continue;
                }

                void* componentPtr = info->add(&scene, newEntity);
                if(info->deserialize)
                    info->deserialize(componentPtr, componentData);
            }
        }

        return true;
    }
}
