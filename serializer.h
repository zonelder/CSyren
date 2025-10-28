#pragma once
#include <string>

namespace csyren::core::events
{
    struct LoadSceneRequest
    {
        std::string scenePath;
    };


    struct SaveSceneRequest
    {
        std::string filepath;
    };
    struct ReloadSceneRequest {};
}

namespace csyren::render
{
    class ResourceManager;
}

namespace csyren::core
{
    class Scene;
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
        Serializer(core::Scene& scene, render::ResourceManager& rm);

        bool loadScene(const std::string& filepath);
        bool saveScene(const std::string& filepath);

    private:
        core::Scene& _scene;
        render::ResourceManager& _resourceManager;
    };
}
