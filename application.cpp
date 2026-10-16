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

	float randomDegree(float minDeg, float maxDeg)
	{
		float t = (float)rand() / RAND_MAX;
		return minDeg + t * (maxDeg - minDeg);
	}
}

namespace csyren
{
	Application::Application()
	{
	}

	Application::~Application() {}


    bool Application::init(const std::filesystem::path& engineAssets,
                           const std::vector<cstdmf::FileSystem::Mount>& solutionPaths)
    {
        wchar_t executable[32768];
        const auto length = GetModuleFileNameW(nullptr, executable, 32768);
        if (!length || length == 32768) return false;
        const auto directory = std::filesystem::path(executable).parent_path();
        auto& files = cstdmf::FileSystem::instance();
#ifdef CSYREN_ENGINE_ASSETS
        const std::filesystem::path defaultEngineAssets(CSYREN_ENGINE_ASSETS);
#else
        const std::filesystem::path defaultEngineAssets("assets");
#endif
#ifdef CSYREN_GAME_ASSETS
        const std::filesystem::path gameAssets(CSYREN_GAME_ASSETS);
#else
        const std::filesystem::path gameAssets("assets");
#endif
        if (!files.mount("engine", engineAssets.empty() ? defaultEngineAssets : engineAssets, false)
            || !files.mount("project", gameAssets, true)
            || !files.mount("cache", directory / "cache", true, false))
            return false;
        for (const auto& mount : solutionPaths)
            if (!files.mount(mount.name, mount.root, mount.writable, mount.searchable))
                return false;
        return true;
    }

	int Application::run()
	{
		log::init();
		log::info("-------------------------------------Init Application-------------------------------------");

		using namespace core;
		
		registerAllComponents();
		editor::EditorRegistry::resolveAll();
		core::details::TimeHandler timeHandler;

		core::Entity::ID currentCameraEntt;

		core::details::ServiceRegistry::create<core::Window>(1200, 786, L"csyren engine");
		core::details::ServiceRegistry::create<core::Time>();
		core::details::ServiceRegistry::create<core::CameraContextService>([&currentCameraEntt]() { return currentCameraEntt; });
		core::details::ServiceRegistry::create<core::EventBus2>();
		auto bus = core::Services::get<core::EventBus2>();
		core::details::ServiceRegistry::create<core::Scene>(bus);
		core::details::ServiceRegistry::create<Serializer>();
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
		auto scene = core::Services::get<core::Scene>();
		auto time = core::Services::get<core::Time>();
		auto window = core::Services::get<core::Window>();
		auto systems = core::Services::get<core::SystemManager>();
		auto stats = core::Services::get<StatisticMonitor>();
		core::details::ServiceRegistry::initializeAll();
		log::info("-------------------------------------------------------------------------------------------");
		log::info("-------------------------------Setup Start Up------------------------------------------------");
		window->show();

		renderer->beginResourceUpload();
		render::Primitives::registerFabricsAll();
		onSceneStart();
		//TODO(dx12) а как работать с динамическими системами?
		//_systems.init();

		renderer->endResourceUpload();
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
					systems->shutdown();
					core::details::ServiceRegistry::shutdownAll();
					log::shutdown();
					return static_cast<int>(msg.wParam);
				}
				TranslateMessage(&msg);
				DispatchMessage(&msg);
			}

			systems->update();
			auto [mainCameraID,camera,cameraTransform] = *(scene->view<Camera,Transform>().begin());//only first camera accepted
			currentCameraEntt = mainCameraID;
			systems->onFrame();

			scene->flush();
			bus->commit_batch();
		}
	}


	/**
	 * @brief method where you can place you custom scene initialization.
	 */
	void Application::onSceneStart()
	{

		srand(time(0));
		using namespace core;
		using namespace render;
		auto res = core::Services::get<render::ResourceManager>();
		auto scene = core::Services::get<core::Scene>();
		auto bus = core::Services::get<core::EventBus2>();
		auto window = core::Services::get<core::Window>();
		auto ser = core::Services::get<Serializer>();
		auto systems = core::Services::get<core::SystemManager>();

		ser->loadSystems("system.config", *systems);
		//------------------------------------LOAD SCENE------------------------------------------------

		auto texture = res->get<render::Texture>("textures/default/red.dds");

		auto texture1 = res->get<render::Texture>("textures/default/white.dds");

		auto texture2 = res->get<render::Texture>("textures/materials/carpet/carpet04.dds");
		//------------------------------------Camera----------------------------------------------------
		auto mainCameraEntt = scene->createEntity("main camera");
		auto mainCamera = scene->addComponent<Camera>(mainCameraEntt);
		auto cameraTransform = scene->addComponent<Transform>(mainCameraEntt);
		auto editorCameraController = scene->addComponent<EditorCameraController>(mainCameraEntt);
		editorCameraController->movementSpeed = 1.0f;
		mainCamera->aspectRatio = window->width() / window->height();
		mainCamera->background = math::Vector4{ 1.f,0.0f,0.0f,1.0f };
		cameraTransform->position = math::Vector3::back * 4 + math::Vector3::up*2;

		//-------------------------------------Material and mesh----------------------------------------
		auto matDefault = render::Primitives::getDefaultMaterial();
		//*
		auto matRainbow = render::Primitives::getRainbowMaterial();
		res->getMaterial(matDefault)->setVector("tint", DirectX::XMFLOAT4(1, 1, 1,1));
		res->getMaterial(matRainbow)->setVector("tint", DirectX::XMFLOAT4(1, 1, 1, 1));
		auto meshQuad = render::Primitives::getQuad();
		auto meshCube = render::Primitives::getCube();

		//---------------------------------------------------------------------------------------------
		auto saveComponent = scene->createEntity("Save");
		auto saveReq = scene->addComponent<SceneLoaderRequest>(saveComponent);
		saveReq->type = SceneLoaderRequest::SAVE;
		saveReq->path = "test_scene.scene";

		float containerHalfX = 5.0f;
		float containerHalfY = 3.0f; // высота ящика
		float containerHalfZ = 5.0f;
		float wallThickness = 0.5f;
		auto createInvisibleWall = [&](Vector3 position, Vector3 size) {
			auto wall = scene->createEntity("Wall");
			auto tr = scene->addComponent<core::Transform>(wall);
			tr->position = position;
			tr->scale = size;

			auto collider = scene->addComponent<physics::BoxCollider>(wall);
			collider->size = size;

			auto rb = scene->addComponent<physics::RigidBody>(wall, physics::RigidBody{ physics::BodyType::Static });
			};

		createInvisibleWall(Vector3{ -containerHalfX - wallThickness / 2, containerHalfY / 2, 0 }, Vector3{ wallThickness, containerHalfY * 2, containerHalfZ * 2 });
		createInvisibleWall(Vector3{ containerHalfX + wallThickness / 2, containerHalfY / 2, 0 }, Vector3{ wallThickness, containerHalfY * 2, containerHalfZ * 2 });

		createInvisibleWall(Vector3{ 0, containerHalfY / 2, -containerHalfZ - wallThickness / 2 }, Vector3{ containerHalfX * 2, containerHalfY * 2, wallThickness });
		createInvisibleWall(Vector3{ 0, containerHalfY / 2, containerHalfZ + wallThickness / 2 }, Vector3{ containerHalfX * 2, containerHalfY * 2, wallThickness });


		auto ground = scene->createEntity("ground");
		auto box = scene->createEntity("box");

		{
			auto tr = scene->addComponent<core::Transform>(ground);
			tr->position = Vector3{ 0.0f, -1.0f, 0.0f };
			tr->scale = Vector3(10, 1, 10);
			auto collider = scene->addComponent<physics::BoxCollider>(ground);
			collider->size = Vector3{ 10.0f, 1.0f, 10.0f };
			auto rb = scene->addComponent<physics::RigidBody>(ground, physics::RigidBody{ physics::BodyType::Static });
			auto cubeRenderer = scene->addComponent<render::MeshRenderer>(ground);
			cubeRenderer->material = matDefault;
			auto cubeMesh = scene->addComponent<render::MeshFilter>(ground);
			cubeMesh->mesh = meshCube;
		}

		{
			//*
			auto textured = scene->createEntity("textured");

			auto tr = scene->addComponent<core::Transform>(textured);
			tr->rotation = Quaternion::euler(-45, 0, 0);
			tr->scale = Vector3(3, 1, 3);
			auto renderer = scene->addComponent<render::MeshRenderer>(textured);
			renderer->material = render::Primitives::getTextureMaterial();
			auto mat = res->getMaterial(renderer->material);
			mat->setTexture("diffuseTexture", texture2);
			auto mesh = scene->addComponent<render::MeshFilter>(textured);
			mesh->mesh = render::Primitives::getQuad();
			//*/
		}

		const int numCubesX = 5;
		const int numCubesY = 5;
		const int numCubesZ = 5;
		float spacing = 1.2f;
		Vector3 basePos{ 0,3,0 };
		Vector3 scale{ 0.5,0.5,0.5 };
		physics::RigidBody templateRB;
		templateRB.type = physics::BodyType::Dynamic;
		templateRB.mass = 5.0f;
		for (int x = 0; x < numCubesX; ++x)
			for (int y = 0; y < numCubesY; ++y)
				for (int z = 0; z < numCubesZ; ++z)
				{
					auto cube = scene->createEntity("dynamic cube");
					auto tr = scene->addComponent<core::Transform>(cube);
					tr->position = Vector3{
						(x - numCubesX / 2) * spacing,
						1.0f + y * spacing,
						(z - numCubesZ / 2) * spacing
					};
					tr->position += basePos;

					tr->scale = scale;

					float angleX = randomDegree(-15.0f, 15.0f);
					float angleY = randomDegree(-15.0f, 15.0f);
					float angleZ = randomDegree(-15.0f, 15.0f);
					tr->rotation = math::Quaternion::euler(Vector3{ angleX, angleY, angleZ });


					auto collider = scene->addComponent<physics::BoxCollider>(cube);//should be a bug here as physic cant update this data.
					collider->size = scale;
					auto rb = scene->addComponent<physics::RigidBody>(cube, templateRB);
					auto meshRenderer = scene->addComponent<render::MeshRenderer>(cube);
					meshRenderer->material = matRainbow;
					auto meshFilter = scene->addComponent<render::MeshFilter>(cube);
					meshFilter->mesh = meshCube;
				}
		//*/
	}
}