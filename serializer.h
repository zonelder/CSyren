#pragma once
#include <string>
#include "scene_storage.h"

namespace csyren::core
{
    class SystemManager;
    class Scene;
    class ArchiveWriter;
    class ArchiveReader;
    class SerializationContext;
}

namespace csyren::core
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
        bool snapshot(core::Scene&, std::string&);
        bool restore(const std::string&, core::Scene&);

        bool saveScene(core::ArchiveWriter&, core::SerializationContext&, core::Scene&);
        bool loadScene(core::ArchiveReader&, core::SerializationContext&, core::Scene&);

        bool loadSystems(const std::string& path, core::SystemManager& systems);
        bool loadSystems(const std::string& appPath, const std::string& editorPath, core::SystemManager& systems);
        bool saveSystems(const std::string& path,const core::SystemManager& systems);
        bool saveSystems(const std::string& appPath, const std::string& editorPath, const core::SystemManager& systems);
        bool loadScene(const std::string& filepath, core::Scene & scene);
        bool saveScene(const std::string& filepath, core::Scene & scene);
    };
    class ArchiveSceneStorage final : public SceneStorage
    {
    public:
        explicit ArchiveSceneStorage(Serializer& serializer) : _serializer(serializer) {}
        bool read(const std::string& path, core::Scene& scene) override { return _serializer.loadScene(path, scene); }
        bool write(const std::string& path, core::Scene& scene) override { return _serializer.saveScene(path, scene); }
        bool snapshot(core::Scene& scene, std::string& archive) override { return _serializer.snapshot(scene, archive); }
        bool restore(const std::string& archive, core::Scene& scene) override { return _serializer.restore(archive, scene); }
    private:
        Serializer& _serializer;
    };
}
