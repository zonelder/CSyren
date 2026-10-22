#pragma once
#include "core/typed_system.h"
#include "core/services.h"
#include "scene_session.h"
#include "serializer.h"
#include "core/registration.h"

CS_STRICT_SYSTEMS_BEGIN
namespace csyren
{
    // Compatibility only. New editor actions enqueue explicit session operations;
    // the session applies them between frames, so this system only queues them.
    class SceneLoaderSystem : public core::TypedSystem<SceneLoaderSystem>
    {
    public:
        static constexpr auto phase = "scene";
        static constexpr core::TaskAffinity affinity = core::TaskAffinity::MainThread;

        void execute(core::Select<SceneLoaderRequest> requests, core::Delete remove, core::Res<editor::SceneSession> sessionService)
        {
            auto& session = *sessionService;
            if (session.playing()) return;
            for (auto [entity, request] : requests)
            {
                switch (request.type)
                {
                case SceneLoaderRequest::LOAD: session.enqueue(editor::SceneOperation::Open, request.path); break;
                case SceneLoaderRequest::RELOAD: session.enqueue(editor::SceneOperation::Reload); break;
                case SceneLoaderRequest::SAVE:
                    session.enqueue(request.path.empty() ? editor::SceneOperation::Save : editor::SceneOperation::SaveAs, request.path);
                    break;
                }
                remove.remove<SceneLoaderRequest>(entity);
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
    CS_SYSTEM(SceneLoaderSystem);
}
CS_STRICT_SYSTEMS_END
