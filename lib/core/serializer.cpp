#include "pch.h"
#include "serializer.h"
#include "serialize_common.h"


#include <fstream>

namespace csyren::core
{
    Serializer::Serializer(Scene& scene, render::ResourceManager& rm) :
        _scene(scene),
        _resourceManager(rm)
    {}

    bool Serializer::saveScene(const std::string& filepath)
    {
        json sceneJson;
        json entitiesArray = json::array();

        for (auto entt : _scene.entities())
        {
            json entityJson;
            entityJson["id"] = entt.id;

            json componentsJson;

            for (const auto& [name, info] :reflection::ComponentRegistry::getAll())
            {
                if (info.has(_scene, entt.id))
                {
                    json componentJson;
                    info.serialize(info.get(_scene, entt.id), componentJson, sceneJson, _resourceManager);
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
    bool Serializer::loadScene(const std::string& filepath)
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

        for (const auto& entityData : data["entities"])
        {
            Entity::ID newEntity = _scene.createEntity();

            if(!entityData.contains("components")) continue;

            for (const auto& [name, componentData] : entityData["components"].items())
            {
                const auto info = reflection::ComponentRegistry::get(name);

                if (!info)
                {
                    log::warning("Serializer::loadScene : Skipping unknown component \'{}\' while loading scene {}", name, filepath);
                    continue;
                }

                void* componentPtr = info->add(_scene, newEntity);
                info->deserialize(componentPtr, componentData, data, _resourceManager);
            }
        }

        return true;
    }
}
