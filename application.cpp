#include "application.h"

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
#include "debug_rotator_system.h"
#include "scene_loader.h"
#include "scene_session.h"
#include "editor_view.h"


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

namespace
{

	DirectX::XMMATRIX createProjection(csyren::core::Camera& camera)
	{

		using namespace DirectX;
		using namespace csyren::core;

        return camera.projectionMatrix();
	}

}

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
        return true;
	}

	int Application::run()
	{

		log::info("-------------------------------------Init Application-------------------------------------");

		using namespace core;
		
		core::details::ServiceRegistry::create<core::ProjectPaths>(_project.paths());
        registerAllComponents();
		editor::EditorRegistry::resolveAll();
		core::details::TimeHandler timeHandler;

		core::Entity::ID currentCameraEntt = core::Entity::invalidID;

		core::details::ServiceRegistry::create<core::Window>(1200, 786, L"csyren engine");
		core::details::ServiceRegistry::create<core::Time>();
		core::details::ServiceRegistry::create<core::CameraContextService>([&currentCameraEntt]() { return currentCameraEntt; });
		core::details::ServiceRegistry::create<Serializer>();
        core::details::ServiceRegistry::create<ArchiveSceneStorage>(*core::Services::get<Serializer>());
        core::details::ServiceRegistry::create<editor::SceneSession>(*core::Services::get<ArchiveSceneStorage>());
        core::details::ServiceRegistry::create<editor::EditorView>();
		core::details::ServiceRegistry::create<physics::PhysicsEngine>();
		core::details::ServiceRegistry::create<core::Devices>();
		core::details::ServiceRegistry::create<core::SystemManager>();
		core::details::ServiceRegistry::create<render::DescriptorManager>();
		core::details::ServiceRegistry::create<render::Renderer>();
		core::details::ServiceRegistry::create<render::ResourceManager>();
		core::details::ServiceRegistry::create<render::details::PSOFactory>();
		core::details::ServiceRegistry::create<editor::EditorSelection>();
		core::details::ServiceRegistry::create<StatisticMonitor>();



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
		MSG msg = { 0 };

		while (true)
		{
			timeHandler.update(*time);
			stats->update(*time);
			window->preMessagePump();
			while (PeekMessage(&msg, 0, 0, 0, PM_REMOVE))
			{
				if (msg.message == WM_QUIT) 
				{
					log::info("---------------------------------------------------------------------------------------------");

					log::info("-------------------------------Shutdown------------------------------------------------------");
					systems->unbind();
                    systems->shutdown();
					core::details::ServiceRegistry::shutdownAll();
					return static_cast<int>(msg.wParam);
				}
				TranslateMessage(&msg);
				DispatchMessage(&msg);
			}

			systems->update();
			auto* editorView = core::Services::get<editor::EditorView>();
            editorView->camera.aspectRatio = static_cast<float>(window->width()) / (std::max)(1, window->height());
            Camera* activeCamera = &editorView->camera;
            Transform* activeTransform = &editorView->transform;
            currentCameraEntt = Entity::invalidID;
            if (session->playing())
            {
                for (auto [id, camera, transform] : session->activeContext().scene.view<Camera, Transform>())
                {
                    if (currentCameraEntt == Entity::invalidID || camera.priority > activeCamera->priority)
                    { currentCameraEntt = id; activeCamera = &camera; activeTransform = &transform; }
                }
            }
            auto& camera = *activeCamera;
            auto& cameraTransform = *activeTransform;
			auto engineVariables = renderer->getEngineVariableBuffer();
			engineVariables->totalTime = time->totalTime();
			DirectX::XMMATRIX invView = cameraTransform.world();
			DirectX::XMStoreFloat4x4(&engineVariables->invViewMatrix, invView);

			// viewMatrix = inverse(invViewMatrix)
			DirectX::XMMATRIX view = DirectX::XMMatrixInverse(nullptr, invView);
			DirectX::XMStoreFloat4x4(&engineVariables->viewMatrix, view);

			// projectionMatrix = projection
			DirectX::XMMATRIX proj = createProjection(camera);
			DirectX::XMStoreFloat4x4(&engineVariables->projectionMatrix, proj);

			// viewProjectionMatrix = view * projection
			DirectX::XMMATRIX viewProj = DirectX::XMMatrixMultiply(view, proj);
			DirectX::XMStoreFloat4x4(&engineVariables->viewProjectionMatrix, viewProj);


			core::Services::get<render::ResourceManager>()->update();
			renderer->beginFrame();
			ID3D12DescriptorHeap* heaps[] = { core::Services::get<render::DescriptorManager>()->shaderHeap()};
			renderer->commandList()->SetDescriptorHeaps(1, heaps);
			renderer->clear(&(camera.background.x));

			systems->onFrame();

			renderer->endFrame();

			session->settle();
            session->processRequests();
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
