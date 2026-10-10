#pragma once
#include "core/system_base.h"
#include "core/services.h"
#include "scene_session.h"
#include "serializer.h"

namespace csyren
{
    // Compatibility only. New editor actions enqueue explicit session operations.
    class SceneLoaderSystem : public core::System
    {
    public:
        void update() override
        {
            auto& session = *core::Services::get<editor::SceneSession>();
            if (session.playing()) return;
            for (auto [entity, request] : scene().view<SceneLoaderRequest>())
            {
                switch (request.type)
                {
                case SceneLoaderRequest::LOAD: session.enqueue(editor::SceneOperation::Open, request.path); break;
                case SceneLoaderRequest::RELOAD: session.enqueue(editor::SceneOperation::Reload); break;
                case SceneLoaderRequest::SAVE:
                    session.enqueue(request.path.empty() ? editor::SceneOperation::Save : editor::SceneOperation::SaveAs, request.path);
                    break;
                }
                scene().removeComponent<SceneLoaderRequest>(entity);
            }
        }
    protected:
        void onAttach() override
        {
            auto& session = *core::Services::get<editor::SceneSession>();
            _tokens.push_back(sceneBus().subscribe<core::LoadSceneRequest>([&session](const auto& request) {
                if (request.scene == &session.editorScene()) session.enqueue(editor::SceneOperation::Open, request.scenePath);
            }));
            _tokens.push_back(sceneBus().subscribe<core::ReloadSceneRequest>([&session](const auto& request) {
                if (request.scene == &session.editorScene()) session.enqueue(editor::SceneOperation::Reload);
            }));
            _tokens.push_back(sceneBus().subscribe<core::SaveSceneRequest>([&session](const auto& request) {
                if (request.scene == &session.editorScene()) session.enqueue(
                    request.filepath.empty() ? editor::SceneOperation::Save : editor::SceneOperation::SaveAs, request.filepath);
            }));
        }
        void onDetach() noexcept override
        {
            for (auto token : _tokens) sceneBus().unsubscribe(token);
            _tokens.clear();
        }
    private:
        std::vector<core::SubscriberToken> _tokens;
    };
    REGISTER_SYSTEM(SceneLoaderSystem)
}
