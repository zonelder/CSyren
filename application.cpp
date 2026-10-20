#include "imGui/imgui_internal.h"
#include "dx12_graphic/gpu_resources.h"
#include "application.h"
#include "core/system_manager.h"
#include "core/window.h"
#include "serializer.h"

#include <cassert>
#include <format>
#include <iostream>

#include "cstdmf/log.h"
#include "math/math.h"

#include "meta_common.h"

#include "core/serialize_common.h"
#include "dx12_graphic/serialize_common.h"

#include "dx12_graphic/primitives.h"
#include "dx12_graphic/mesh.h"
#include "dx12_graphic/material.h"

#include "core/event_bus.h"
#include "core/time.h"
#include "core/camera.h"
#include "core/transform.h"
#include "core/camera_service.h"

#include "core/input_dispatch_system.h"
#include "mesh_render_system.h"
#include "editor_camera_controller_system.h"
#include "scene_loader.h"
#include "scene_session.h"
#include "editor_viewport_state.h"


#include "physics/rigid_body.h"
#include "physics/colliders.h"
#include "physics/spring_join.h"

#include "physics/spring_join_system.h"

#include "dx12_graphic/texture.h"

#include "dx12_graphic/resource_manager.h"
#include "dx12_graphic/descriptors.h"
#include "dx12_graphic/pso_factory.h"


#include "system_registrator.h"
#include "statistic_monitor.h"

#include "math_editor.h"

#include "component_registrator.h"
#include "editor_overlays.h"

namespace csyren
{
	Application::Application()
	{
	}

	Application::~Application() {}


	bool Application::init(const std::filesystem::path& project)
	{

        log::info("Loading project '{}' (working directory '{}')",
            core::pathToUtf8(std::filesystem::absolute(project)), core::pathToUtf8(std::filesystem::current_path()));
        _project = ProjectSettings::load(project);
        log::info("Project '{}' loaded; root='{}', assets='{}'", _project.name,
            core::pathToUtf8(_project.paths().root), core::pathToUtf8(_project.paths().assets));
        auto& files = cstdmf::FileSystem::instance();
        const auto paths = _project.paths();
#ifdef CSYREN_ENGINE_ASSETS
        const std::filesystem::path engineAssets(CSYREN_ENGINE_ASSETS);
#else
        const auto engineAssets = paths.assets;
#endif
        return files.mount("engine", engineAssets, false)
            && files.mount("project", paths.assets, true)
            && files.mount("cache", paths.build({}), true, false);
	}

	int Application::run()
	{

		log::info("-------------------------------------Init Application-------------------------------------");

		using namespace core;
		
		core::details::ServiceRegistry::create<core::ProjectPaths>(_project.paths());
        registerAllComponents();
		editor::EditorRegistry::resolveAll();
		core::details::TimeHandler timeHandler;



		core::details::ServiceRegistry::create<core::Window>(1200, 786, L"csyren engine");
		core::details::ServiceRegistry::create<core::Time>();
		core::Services::get<core::Time>()->fixedDeltaTime = 1.0f / 120.0f; // physics + character step
		core::details::ServiceRegistry::create<core::CameraContextService>([]() {
            auto* session = core::Services::get<editor::SceneSession>();
            auto id = core::Entity::invalidID;
            int priority = (std::numeric_limits<int>::min)();
            if (session->playing()) for (auto [candidate, camera] : session->activeContext().scene.view<const core::Camera>())
                if (id == core::Entity::invalidID || camera.priority > priority) { id = candidate; priority = camera.priority; }
            return id;
        });
		core::details::ServiceRegistry::create<Serializer>();
        core::details::ServiceRegistry::create<ArchiveSceneStorage>(*core::Services::get<Serializer>());
        core::details::ServiceRegistry::create<editor::SceneSession>(*core::Services::get<ArchiveSceneStorage>());
        core::details::ServiceRegistry::create<editor::EditorViewportState>();
		core::details::ServiceRegistry::create<physics::PhysicsLayers>(); // layer names and collision rules are defined by the project
		core::details::ServiceRegistry::create<physics::PhysicsEngine>();
		core::details::ServiceRegistry::create<core::Devices>();
		core::details::ServiceRegistry::create<core::SystemManager>();
		core::details::ServiceRegistry::create<render::DescriptorManager>();
		core::details::ServiceRegistry::create<render::Renderer>();
        core::details::ServiceRegistry::create<render::GpuResources>();
		core::details::ServiceRegistry::create<render::ResourceManager>();
		core::details::ServiceRegistry::create<render::details::PSOFactory>();
		core::details::ServiceRegistry::create<editor::EditorSelection>();
		core::details::ServiceRegistry::create<StatisticMonitor>();
		core::details::ServiceRegistry::create<editor::EditorOverlays>();



		auto renderer = core::Services::get<render::Renderer>();
		auto session = core::Services::get<editor::SceneSession>();
		auto time = core::Services::get<core::Time>();
		auto window = core::Services::get<core::Window>();
		auto systems = core::Services::get<core::SystemManager>();
		auto stats = core::Services::get<StatisticMonitor>();
		session->setBind([systems](auto editor, auto active, bool playing) { systems->bind(editor, active, playing); });

        core::details::ServiceRegistry::initializeAll();
		log::info("-------------------------------------------------------------------------------------------");
		log::info("-------------------------------Setup Start Up------------------------------------------------");
		// Show after InputDispatchSystem has installed the window dispatcher.

		renderer->beginResourceUpload();
		onSceneStart();
		renderer->endResourceUpload();
        session->settle();
        window->show();

		log::info("---------------------------------------------------------------------------------------------");
		log::info("-------------------------------Run Game Loop-------------------------------------------------");
        const auto shutdown = [&] {
            systems->unbind(); systems->shutdown(); core::details::ServiceRegistry::shutdownAll();
        };
        MSG msg{};
        while (true) {
            window->preMessagePump();
            while (PeekMessage(&msg,nullptr,0,0,PM_REMOVE)) {
                if (msg.message == WM_QUIT) {
                    log::info("-------------------------------Shutdown------------------------------------------------------");
                    shutdown(); return static_cast<int>(msg.wParam);
                }
                TranslateMessage(&msg); DispatchMessage(&msg);
            }
            try {
                timeHandler.update(*time); stats->update(*time);
                systems->execute(); session->settle(); session->processRequests();
            }
            catch (...) { shutdown(); throw; }
        }
	}


    void Application::onSceneStart()
    {
        auto* serializer = core::Services::get<Serializer>();
        auto* systems = core::Services::get<core::SystemManager>();
        const auto paths = _project.paths();

        log::info("Loading systems: app='{}', editor='{}'",
            core::pathToUtf8(paths.resolve(core::pathFromUtf8(_project.appSystems))),
            core::pathToUtf8(paths.resolve(core::pathFromUtf8(_project.editorSystems))));
        if (!serializer->loadSystems(core::pathToUtf8(paths.resolve(core::pathFromUtf8(_project.appSystems))),
            core::pathToUtf8(paths.resolve(core::pathFromUtf8(_project.editorSystems))), *systems))
            throw std::runtime_error("Failed to load project systems");
        // Restore the demo material setup before the archive resolves its resource handles.
        auto* resources = core::Services::get<render::ResourceManager>();
        auto texture = resources->get<render::Texture>("textures/materials/carpet/carpet04.dds");
        auto material = render::Primitives::getTextureMaterial();
        resources->getMaterial(material)->setTexture("diffuseTexture", texture);
        if (!_project.startScene.empty())
        {

            log::info("Loading start scene '{}'", core::pathToUtf8(paths.resolve(core::pathFromUtf8(_project.startScene))));
            auto* session = core::Services::get<editor::SceneSession>();
            session->enqueue(editor::SceneOperation::Open,
                core::pathToUtf8(paths.resolve(core::pathFromUtf8(_project.startScene))));
            session->processRequests();
            if (!session->error().empty()) throw std::runtime_error(session->error());
        }
    }
}
