#pragma once
#include <string>

namespace csyren::core
{
    class Scene;
}

namespace csyren::core::events
{
    struct LoadSceneRequest
    {
        std::string scenePath;
        Scene* scene;
    };


    struct SaveSceneRequest
    {
        std::string filepath;
        Scene* scene;
    };
    struct ReloadSceneRequest { Scene* scene; };
}

namespace csyren::render
{
    class ResourceManager;
}



namespace csyren
{

    struct SceneLoaderRequest
    {
        enum RequestType
        {
            LOAD,
            RELOAD,
            SAVE,
        };
        RequestType type;
        std::string path;
    };

    class Serializer
    {
    public:
        bool loadScene(const std::string& filepath, core::Scene & scene);
        bool saveScene(const std::string& filepath, core::Scene & scene);
    };
}
