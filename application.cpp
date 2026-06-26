#include "application.h"

#include <cassert>
#include <format>
#include <iostream>

#include "cstdmf/log.h"
#include "math/math.h"

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

namespace
{

	DirectX::XMMATRIX createProjection(csyren::core::components::Camera& camera)
	{

		using namespace DirectX;
		using namespace csyren::core::components;

		if (camera.projection == ProjectionType::Perspective)
		{
			return csyren::math::Matrix4x4::perspective(camera.fov, camera.aspectRatio, camera.near, camera.far);
		}
		else // Orthographic
		{
			// Interpret FOV as vertical height of view volume
			float viewHeight = camera.fov;
			float viewWidth = viewHeight * camera.aspectRatio;
			return DirectX::XMMatrixOrthographicLH(viewWidth, viewHeight, camera.near, camera.far);
		}
	}

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


	bool Application::init()
	{
		return true;
	}

	int Application::run()
	{
		log::init();
		log::info("-------------------------------------Init Application-------------------------------------");

		using namespace core::components;
		core::details::TimeHandler timeHandler;

		core::Entity::ID currentCameraEntt;

		core::details::ServiceRegistry::create<core::Window>(1200, 786, L"csyren engine");
		core::details::ServiceRegistry::create<core::Time>();
		core::details::ServiceRegistry::create<core::CameraContextService>([&currentCameraEntt]() { return currentCameraEntt; });
		core::details::ServiceRegistry::create<core::events::EventBus2>();
		core::details::ServiceRegistry::create<core::Scene>();
		core::details::ServiceRegistry::create<Serializer>();
		core::details::ServiceRegistry::create<physics::PhysicsEngine>();
		core::details::ServiceRegistry::create<core::input::Devices>();
		core::details::ServiceRegistry::create<render::DescriptorManager>();
		core::details::ServiceRegistry::create<render::Renderer>();
		core::details::ServiceRegistry::create<render::ResourceManager>();
		core::details::ServiceRegistry::create<render::details::PSOFactory>();

		auto bus = core::Services::get<core::events::EventBus2>();
		auto renderer = core::Services::get<render::Renderer>();
		auto scene = core::Services::get<core::Scene>();
		auto time = core::Services::get<core::Time>();
		auto window = core::Services::get<core::Window>();
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
		const FLOAT clearColor[4] = { 0.1f, 0.1f, 0.3f, 1.0f };
		MSG msg = { 0 };

		while (true)
		{
			timeHandler.update(*time);
			window->preMessagePump();
			while (PeekMessage(&msg, 0, 0, 0, PM_REMOVE))
			{
				if (msg.message == WM_QUIT) 
				{
					log::info("---------------------------------------------------------------------------------------------");

					log::info("-------------------------------Shutdown------------------------------------------------------");
					_systems.shutdown();
					core::details::ServiceRegistry::shutdownAll();
					log::shutdown();
					return static_cast<int>(msg.wParam);
				}
				TranslateMessage(&msg);
				DispatchMessage(&msg);
			}

			_systems.update();
			auto [mainCameraID,camera,cameraTransform] = *(scene->view<Camera,Transform>().begin());//only first camera accepted
			currentCameraEntt = mainCameraID;
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

			float rotationSpeed = DirectX::XM_2PI / 10.0f;
			float angle = time->totalTime() * rotationSpeed;
			DirectX::XMVECTOR baseDir = DirectX::XMVectorSet(0.3f, -1.0f, 0.3f, 0.0f);
			DirectX::XMMATRIX rot = DirectX::XMMatrixRotationZ(angle);
			DirectX::XMVECTOR rotatedDir = DirectX::XMVector3TransformNormal(baseDir, rot);
			rotatedDir = DirectX::XMVector3Normalize(rotatedDir);
			DirectX::XMStoreFloat4(&engineVariables->lightDirection, rotatedDir);

			core::Services::get<render::ResourceManager>()->update();
			renderer->beginFrame();
			ID3D12DescriptorHeap* heaps[] = { core::Services::get<render::DescriptorManager>()->shaderHeap()};
			renderer->commandList()->SetDescriptorHeaps(1, heaps);
			renderer->clear(&(camera.background.x));

			_systems.onFrame();

			renderer->endFrame();

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
		using namespace core::components;
		using namespace render::components;
		auto res = core::Services::get<render::ResourceManager>();
		auto scene = core::Services::get<core::Scene>();
		auto bus = core::Services::get<core::events::EventBus2>();
		auto window = core::Services::get<core::Window>();
		auto ser = core::Services::get<Serializer>();

		ser->loadSystems("system.config", _systems);
		//------------------------------------LOAD SCENE------------------------------------------------

		auto texture = res->get<render::Texture>("E:\\stalker_online_git\\res\\textures\\default\\red.dds");

		auto texture1 = res->get<render::Texture>("E:\\stalker_online_git\\res\\textures\\default\\white.dds");

		auto texture2 = res->get<render::Texture>("E:\\stalker_online_git\\res\\textures\\materials\\carpet\\carpet04.dds");
		//------------------------------------Camera----------------------------------------------------
		auto mainCameraEntt = scene->createEntity();
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
		auto saveComponent = scene->createEntity();
		auto saveReq = scene->addComponent<SceneLoaderRequest>(saveComponent);
		saveReq->type = SceneLoaderRequest::SAVE;
		saveReq->path = "test_scene.scene";

		float containerHalfX = 5.0f;
		float containerHalfY = 3.0f; // высота ящика
		float containerHalfZ = 5.0f;
		float wallThickness = 0.5f;
		auto createInvisibleWall = [&](Vector3 position, Vector3 size) {
			auto wall = scene->createEntity();
			auto tr = scene->addComponent<core::components::Transform>(wall);
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


		auto ground = scene->createEntity();
		auto box = scene->createEntity();

		{
			auto tr = scene->addComponent<core::components::Transform>(ground);
			tr->position = Vector3{ 0.0f, -1.0f, 0.0f };
			tr->scale = Vector3(10, 1, 10);
			auto collider = scene->addComponent<physics::BoxCollider>(ground);
			collider->size = Vector3{ 10.0f, 1.0f, 10.0f };
			auto rb = scene->addComponent<physics::RigidBody>(ground, physics::RigidBody{ physics::BodyType::Static });
			auto cubeRenderer = scene->addComponent<render::components::MeshRenderer>(ground);
			cubeRenderer->material = matDefault;
			auto cubeMesh = scene->addComponent<render::components::MeshFilter>(ground);
			cubeMesh->mesh = meshCube;
		}

		{
			//*
			auto textured = scene->createEntity();

			auto tr = scene->addComponent<core::components::Transform>(textured);
			tr->rotation = Quaternion::euler(-45, 0, 0);
			tr->scale = Vector3(3, 1, 3);
			auto renderer = scene->addComponent<render::components::MeshRenderer>(textured);
			renderer->material = render::Primitives::getTextureMaterial();
			auto mat = res->getMaterial(renderer->material);
			mat->setTexture("diffuseTexture", texture2);
			auto mesh = scene->addComponent<render::components::MeshFilter>(textured);
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
					auto cube = scene->createEntity();
					auto tr = scene->addComponent<core::components::Transform>(cube);
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
					auto meshRenderer = scene->addComponent<render::components::MeshRenderer>(cube);
					meshRenderer->material = matRainbow;
					auto meshFilter = scene->addComponent<render::components::MeshFilter>(cube);
					meshFilter->mesh = meshCube;
				}
		//*/
	}
}