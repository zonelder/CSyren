#include "serializer.h"

#include "core/scene.h"
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

            for (const auto& [name, info] : core::reflection::ComponentRegistry::getAll())
            {
                if (info.has(scene, entt.id))
                {
                    json componentJson;
                    info.serialize(info.get(scene, entt.id), componentJson);
                    componentsJson[name] = componentJson;
                }
            }
            entityJson["components"] = componentsJson;
            entitiesArray.push_back(entityJson);

        }
        sceneJson["entities"] = entitiesArray;

        try
        {
            std::ofstream o(filepath);
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

#pragma optimize("",off)
    bool Serializer::loadScene(const std::string& filepath,core::Scene& scene)
    {
        std::ifstream f(filepath);

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
            core::Entity::ID newEntity = scene.createEntity();

            if (!entityData.contains("components")) continue;

            for (const auto& [name, componentData] : entityData["components"].items())
            {
                const auto info = core::reflection::ComponentRegistry::get(name);

                if (!info)
                {
                    log::warning("Serializer::loadScene : Skipping unknown component \'{}\' while loading scene {}", name, filepath);
                    continue;
                }

                void* componentPtr = info->add(scene, newEntity);
                info->deserialize(componentPtr, componentData);
            }
        }

        return true;
    }
}
