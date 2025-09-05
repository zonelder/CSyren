#pragma once
#include "core/event_bus.h"
#include "core/context.h"
#include "core/system_base.h"
#include "core/scene.h"
#include "core/serializer.h"

namespace csyren
{
	class SceneLoaderSystem : public core::System
	{
	public:
		explicit SceneLoaderSystem(core::Serializer& s) : _serializer(s){}

        void init(core::events::SystemEvent& event) override
        {

            _publishTokens.emplace_back() = event.bus.register_publisher<core::events::LoadSceneRequest>();
            _publishTokens.emplace_back() = event.bus.register_publisher<core::events::ReloadSceneRequest>();
            _publishTokens.emplace_back() = event.bus.register_publisher<core::events::SaveSceneRequest>();

            _tokens.emplace_back() = event.bus.subscribe<core::events::LoadSceneRequest>([this](auto request) { this->handleLoadScene(request); });
            _tokens.emplace_back() = event.bus.subscribe<core::events::ReloadSceneRequest>([this](auto request) { this->handleReloadScene(request); });
            _tokens.emplace_back() = event.bus.subscribe<core::events::SaveSceneRequest>([this](auto request) { this->handleSaveScene(request); });
        };

        void shutdown(core::events::SystemEvent& event) override
        {
            for (auto token : _tokens)
            {
                event.bus.unsubscribe(token);
            }

            for (auto token : _publishTokens)
            {
                event.bus.unregister_publisher(token);
            }
        }


        void update(core::events::UpdateEvent& event) override
        {
            std::vector<std::string> saveRequests;
            std::vector<std::string> loadRequests;
            bool needReload = false;

            using RequestType = core::SceneLoaderRequest::RequestType;
            for (auto [entt, req] : event.scene.view<core::SceneLoaderRequest>())
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

                event.scene.removeComponent<core::SceneLoaderRequest>(entt);//temporal component
            }

            for (const auto& path: saveRequests)
            {
                handleSaveScene(core::events::SaveSceneRequest(path));
            }

            if (!loadRequests.empty())
            {
                handleLoadScene(core::events::LoadSceneRequest(loadRequests.back()));
                needReload = false;//we load new scene. reload dont needed;
            }

            if (needReload)
            {
                handleReloadScene(core::events::ReloadSceneRequest{});
            }
        }


    private:
        void handleLoadScene(const core::events::LoadSceneRequest& event)
        {
            log::info("Handling LoadSceneRequest for: {}", event.scenePath);
            if (_serializer.loadScene(event.scenePath))
            {
                _loadedScenePath = event.scenePath;
            }
        }

        void handleReloadScene(const core::events::ReloadSceneRequest& event)
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

        void handleSaveScene(const core::events::SaveSceneRequest& event)
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

		core::Serializer& _serializer;
        std::string _loadedScenePath;
        std::vector<core::events::PublishToken>    _publishTokens;
        std::vector<core::events::SubscriberToken> _tokens;
	};
}
