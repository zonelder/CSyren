#pragma once
#include "core/event_bus.h"
#include "core/context.h"
#include "core/system_base.h"
#include "core/scene.h"
#include "serializer.h"

namespace csyren
{
	class SceneLoaderSystem : public core::System
	{
	public:
		explicit SceneLoaderSystem(Serializer& s) : _serializer(s){}

        void init(ServiceContext& ctx) override
        {
            auto bus = ctx.get<events::EventBus2>();
            _publishTokens.emplace_back() = bus->register_publisher<events::LoadSceneRequest>();
            _publishTokens.emplace_back() = bus->register_publisher<events::ReloadSceneRequest>();
            _publishTokens.emplace_back() = bus->register_publisher<events::SaveSceneRequest>();

            _tokens.emplace_back() = bus->subscribe<events::LoadSceneRequest>([this](auto request) { this->handleLoadScene(request); });
            _tokens.emplace_back() = bus->subscribe<events::ReloadSceneRequest>([this](auto request) { this->handleReloadScene(request); });
            _tokens.emplace_back() = bus->subscribe<events::SaveSceneRequest>([this](auto request) { this->handleSaveScene(request); });
        };

        void shutdown(ServiceContext& ctx) override
        {
            auto bus = ctx.get<events::EventBus2>();
            for (auto token : _tokens)
            {
                bus->unsubscribe(token);
            }

            for (auto token : _publishTokens)
            {
                bus->unregister_publisher(token);
            }
        }


        void update(ServiceContext& ctx) override
        {
            auto scene = ctx.get<core::Scene>();

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
                handleSaveScene(events::SaveSceneRequest(path));
            }

            if (!loadRequests.empty())
            {
                handleLoadScene(events::LoadSceneRequest(loadRequests.back()));
                needReload = false;//we load new scene. reload dont needed;
            }

            if (needReload)
            {
                handleReloadScene(events::ReloadSceneRequest{});
            }
        }


    private:
        void handleLoadScene(const events::LoadSceneRequest& event)
        {
            log::info("Handling LoadSceneRequest for: {}", event.scenePath);
            if (_serializer.loadScene(event.scenePath))
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
            _serializer.loadScene(_loadedScenePath);

        }

        void handleSaveScene(const events::SaveSceneRequest& event)
        {
            if (event.filepath.empty())
            {
                log::error("Handling save of current scene but filepath are not set.");
                return;
            }

            if (_serializer.saveScene(event.filepath))
            {
                log::info("Scene successfully saved to '{}'.", event.filepath);
                _loadedScenePath = event.filepath;//now we work with that new scene.
            }

        }

		Serializer& _serializer;
        std::string _loadedScenePath;
        std::vector<events::PublishToken>    _publishTokens;
        std::vector<events::SubscriberToken> _tokens;
	};
}
