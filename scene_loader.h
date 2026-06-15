#pragma once
#include "core/event_bus.h"
#include "core/services.h"
#include "core/system_base.h"
#include "core/scene.h"
#include "serializer.h"

namespace csyren
{
	class SceneLoaderSystem : public core::System
	{
	public:
        void init() override
        {
            using ctx = core::Services;
            auto bus = ctx::get<events::EventBus2>();
            _publishTokens.emplace_back() = bus->register_publisher<events::LoadSceneRequest>();
            _publishTokens.emplace_back() = bus->register_publisher<events::ReloadSceneRequest>();
            _publishTokens.emplace_back() = bus->register_publisher<events::SaveSceneRequest>();

            _tokens.emplace_back() = bus->subscribe<events::LoadSceneRequest>([this](auto request) { this->handleLoadScene(request); });
            _tokens.emplace_back() = bus->subscribe<events::ReloadSceneRequest>([this](auto request) { this->handleReloadScene(request); });
            _tokens.emplace_back() = bus->subscribe<events::SaveSceneRequest>([this](auto request) { this->handleSaveScene(request); });
        };

        void shutdown() override
        {
            using ctx = core::Services;
            auto bus = ctx::get<events::EventBus2>();
            for (auto token : _tokens)
            {
                bus->unsubscribe(token);
            }

            for (auto token : _publishTokens)
            {
                bus->unregister_publisher(token);
            }
        }


        void update() override
        {
            using ctx = core::Services;
            auto scene = ctx::get<core::Scene>();

            std::vector<std::string> saveRequests;
            std::vector<std::string> loadRequests;
            bool needReload = false;

            using RequestType = SceneLoaderRequest::RequestType;
            for (auto [entt, req] : scene->view<SceneLoaderRequest>())
            {
                switch (req.type)
                {
                case RequestType::SAVE:
                    saveRequests.push_back(req.path);
                    break;
                case RequestType::LOAD:
                    loadRequests.push_back(req.path);
                    break;
                case RequestType::RELOAD:
                    needReload = true;
                    break;
                }

                scene->removeComponent<SceneLoaderRequest>(entt);//temporal component
            }

            for (const auto& path: saveRequests)
            {
                handleSaveScene(events::SaveSceneRequest(path,scene));
            }

            if (!loadRequests.empty())
            {
                handleLoadScene(events::LoadSceneRequest(loadRequests.back(), scene));
                needReload = false;//we load new scene. reload dont needed;
            }

            if (needReload)
            {
                handleReloadScene(events::ReloadSceneRequest{scene});
            }
        }


    private:
        void handleLoadScene(const events::LoadSceneRequest& event)
        {
            using ctx = core::Services;
            auto ser = ctx::get<Serializer>();
            log::info("Handling LoadSceneRequest for: {}", event.scenePath);
            if (ser->loadScene(event.scenePath,*event.scene))
            {
                _loadedScenePath = event.scenePath;
            }
        }

        void handleReloadScene(const events::ReloadSceneRequest& event)
        {
            if (_loadedScenePath.empty())
            {
                log::warning("Cannot reload scene: no current scene is active.");
                return;
            }
            log::info("Handling ReloadCurrentSceneRequest for: {}", _loadedScenePath);
            //todo clear scene.
            using ctx = core::Services;
            auto ser = ctx::get<Serializer>();
            ser->loadScene(_loadedScenePath,*(event.scene));

        }

        void handleSaveScene(const events::SaveSceneRequest& event)
        {
            if (event.filepath.empty())
            {
                log::error("Handling save of current scene but filepath are not set.");
                return;
            }
            using ctx = core::Services;
            auto ser = ctx::get<Serializer>();
            if (ser->saveScene(event.filepath, *(event.scene)))
            {
                log::info("Scene successfully saved to '{}'.", event.filepath);
                _loadedScenePath = event.filepath;//now we work with that new scene.
            }

        }
        std::string _loadedScenePath;
        std::vector<events::PublishToken>    _publishTokens;
        std::vector<events::SubscriberToken> _tokens;
	};
}
