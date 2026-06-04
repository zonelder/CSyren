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
#include "core/context.h"
#include "core/time.h"
#include "core/camera.h"
#include "core/transform.h"
#include "core/input_dispatcher.h"
#include "core/camera_service.h"

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
	Application::Application() :
		_window(1200, 786, L"csyren engine"),
		_inputDispatcher(),
		_bus(std::make_unique<csyren::core::events::EventBus2>()),
		_scene(*_bus)
	{
	}

	Application::~Application() {}


	bool Application::init()
	{
		log::init();
		log::info("-------------------------------------Init Application-------------------------------------");
		auto hWnd = _window.init();
		if (!hWnd) { return false; }
		_window.setInputDispatcher(&_inputDispatcher);

		render::details::SingletonRegistry::add<render::DescriptorManager>();
		render::details::SingletonRegistry::add<render::Renderer>();
		if (!render::Renderer::instance().earlyInit(hWnd, _window.width(), _window.height()))
		{
			return false;
		}
		render::details::SingletonRegistry::add<render::ResourceManager>();
		render::details::SingletonRegistry::add<render::details::PSOFactory>();

		render::details::SingletonRegistry::add<Serializer>();
		_inputDispatcher.init(*_bus);
		render::details::SingletonRegistry::initializeAll(render::Renderer::instance().device());
		log::info("-------------------------------------------------------------------------------------------");
		return true;
	}

	int Application::run()
	{

		_window.show();
		MSG msg = { 0 };

		const FLOAT clearColor[4] = { 0.1f, 0.1f, 0.3f, 1.0f };

		core::Time time;
		using namespace core::components;
		core::details::TimeHandler timeHandler;

		core::Entity::ID currentCameraEntt;
		core::CameraContextService cameraServ([&currentCameraEntt]() { return currentCameraEntt; });
		core::ServiceContext ctx;

		ctx.registerService(&_inputDispatcher.devices());
		ctx.registerService(&_scene);
		ctx.registerService(_bus.get());
		ctx.registerService(&time);
		ctx.registerService(&cameraServ);
		ctx.registerService(&_physics);

		log::info("-------------------------------Setup Start Up------------------------------------------------");
		render::Renderer::instance().beginResourceUpload();
		render::Primitives::registerFabricsAll();
		onSceneStart(ctx);
		_systems.init(ctx);

		render::Renderer::instance().endResourceUpload();
		log::info("---------------------------------------------------------------------------------------------");
		log::info("-------------------------------Run Game Loop-------------------------------------------------");
		while (true)
		{
			timeHandler.update(time);
			_window.preMessagePump();
			while (PeekMessage(&msg, 0, 0, 0, PM_REMOVE))
			{
				if (msg.message == WM_QUIT) 
				{
					log::info("---------------------------------------------------------------------------------------------");

					log::info("-------------------------------Shutdown------------------------------------------------------");
					_systems.shutdown(ctx);
					_inputDispatcher.shutdown(*_bus);
					render::details::SingletonRegistry::shutdownAll();
					log::shutdown();
					return static_cast<int>(msg.wParam);
				}
				TranslateMessage(&msg);
				DispatchMessage(&msg);
			}

			_inputDispatcher.update(*_bus);
			_systems.update(ctx);
			auto& renderer = render::Renderer::instance();
			auto [mainCameraID,camera,cameraTransform] = *(_scene.view<Camera,Transform>().begin());//only first camera accepted
			currentCameraEntt = mainCameraID;
			auto engineVariables = renderer.getEngineVariableBuffer();
			engineVariables->totalTime = time.totalTime();
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
			float angle = time.totalTime() * rotationSpeed;
			DirectX::XMVECTOR baseDir = DirectX::XMVectorSet(0.3f, -1.0f, 0.3f, 0.0f);
			DirectX::XMMATRIX rot = DirectX::XMMatrixRotationZ(angle);
			DirectX::XMVECTOR rotatedDir = DirectX::XMVector3TransformNormal(baseDir, rot);
			rotatedDir = DirectX::XMVector3Normalize(rotatedDir);
			DirectX::XMStoreFloat4(&engineVariables->lightDirection, rotatedDir);

			render::ResourceManager::instance().update();
			renderer.beginFrame();
			ID3D12DescriptorHeap* heaps[] = { render::DescriptorManager::instance().shaderHeap() };
			renderer.commandList()->SetDescriptorHeaps(1, heaps);
			renderer.clear(&(camera.background.x));

			_systems.onFrame(ctx);

			renderer.endFrame();

			_scene.flush();
			_bus->commit_batch();
		}
	}


	/**
	 * @brief method where you can place you custom scene initialization.
	 */
	void Application::onSceneStart(core::ServiceContext& ctx)
	{

		srand(time(0));
		using namespace core::components;
		using namespace render::components;
		auto& res = render::ResourceManager::instance();
		//-----------------------------init systems---------------------------------------------------
		//
		//--------------------------------------------------------------------------------------------
		auto sceneLoaderSystem				= std::make_shared<csyren::SceneLoaderSystem>();
		auto editorCameraControllerSystem	= std::make_shared<csyren::EditorCameraControllerSystem>();
		auto debugRotatorSystem				= std::make_shared<csyren::DebugRotatorSystem>();
		auto meshRenderSystem				= std::make_shared<csyren::MeshRenderSystem>();
		auto physicSystem					= std::make_shared<csyren::physics::PhysicsSystem>();
		auto springJoinSystem				= std::make_shared<csyren::physics::SpringJoinSystem>();

		//----------------------------technical systems block-----------------------------------------
		_systems.addSystem(sceneLoaderSystem, -100);
		//--------------------------------------------------------------------------------------------
		//----------------------------physical systems block------------------------------------------
		_systems.addSystem(physicSystem,-4);
		_systems.addSystem(springJoinSystem, -3);
		//--------------------------------------------------------------------------------------------
		//----------------------------main systems----------------------------------------------------
		_systems.addSystem(debugRotatorSystem, -2);
		_systems.addSystem(editorCameraControllerSystem, -1);
		_systems.addSystem(meshRenderSystem, 0);

		//------------------------------------LOAD SCENE------------------------------------------------

		auto texture = res.get<render::Texture>("E:\\stalker_online_git\\res\\textures\\default\\red.dds");

		auto texture1 = res.get<render::Texture>("E:\\stalker_online_git\\res\\textures\\default\\white.dds");

		auto texture2 = res.get<render::Texture>("E:\\stalker_online_git\\res\\textures\\materials\\carpet\\carpet04.dds");
		//------------------------------------Camera----------------------------------------------------
		auto mainCameraEntt = _scene.createEntity();
		auto mainCamera = _scene.addComponent<Camera>(mainCameraEntt);
		auto cameraTransform = _scene.addComponent<Transform>(mainCameraEntt);
		auto editorCameraController = _scene.addComponent<EditorCameraController>(mainCameraEntt);
		editorCameraController->movementSpeed = 1.0f;
		mainCamera->aspectRatio = _window.width() / _window.height();
		mainCamera->background = math::Vector4{ 1.f,0.0f,0.0f,1.0f };
		cameraTransform->position = math::Vector3::back * 4 + math::Vector3::up*2;

		//-------------------------------------Material and mesh----------------------------------------
		auto matDefault = render::Primitives::getDefaultMaterial();
		//*
		auto matRainbow = render::Primitives::getRainbowMaterial();
		res.getMaterial(matDefault)->setVector("tint", DirectX::XMFLOAT4(1, 1, 1,1));
		res.getMaterial(matRainbow)->setVector("tint", DirectX::XMFLOAT4(1, 1, 1, 1));
		auto meshQuad = render::Primitives::getQuad();
		auto meshCube = render::Primitives::getCube();
		/*
		const int gridX = 100;
		const int gridY = 20;
		const float spacing = 1.5f;
		int counter = 0;

		for (int y = 0; y < gridY; ++y)
		{
			for (int x = 0; x < gridX; ++x)
			{
				auto ent = _scene.createEntity();
				auto tr = _scene.addComponent<Transform>(ent);
				auto mf = _scene.addComponent<MeshFilter>(ent);
				auto mr = _scene.addComponent<MeshRenderer>(ent);

				float fx = (x - gridX / 2.0f) * spacing;
				float fz = (y - gridY / 2.0f) * spacing;
				tr->position = Vector3{ fx, sinf(y * 0.3f) * 0.5f, fz };
				tr->scale = Vector3{ 0.5f, 0.5f, 0.5f };

				bool even = ((x + y) % 2) == 0;
				mf->mesh = even ? meshCube : meshQuad;
				mr->material = even ? matDefault : matRainbow;

				auto rotEnt = _scene.addComponent<DebugRotator>(ent);
				rotEnt->speed = DirectX::XMFLOAT3(0, 0.5f + 0.3f * (x % 5), 0);
			}
		}
		*/


		//---------------------------------------------------------------------------------------------
		auto saveComponent = _scene.createEntity();
		auto saveReq = _scene.addComponent<SceneLoaderRequest>(saveComponent);
		saveReq->type = SceneLoaderRequest::SAVE;
		saveReq->path = "E:\\test_scene.scene";

		float containerHalfX = 5.0f;
		float containerHalfY = 3.0f; // высота ящика
		float containerHalfZ = 5.0f;
		float wallThickness = 0.5f;
		auto createInvisibleWall = [&](Vector3 position, Vector3 size) {
			auto wall = _scene.createEntity();
			auto tr = _scene.addComponent<core::components::Transform>(wall);
			tr->position = position;
			tr->scale = size;

			auto collider = _scene.addComponent<physics::BoxCollider>(wall);
			collider->size = size;

			auto rb = _scene.addComponent<physics::RigidBody>(wall, physics::RigidBody{ physics::BodyType::Static });
			};

		createInvisibleWall(Vector3{ -containerHalfX - wallThickness / 2, containerHalfY / 2, 0 }, Vector3{ wallThickness, containerHalfY * 2, containerHalfZ * 2 });
		createInvisibleWall(Vector3{ containerHalfX + wallThickness / 2, containerHalfY / 2, 0 }, Vector3{ wallThickness, containerHalfY * 2, containerHalfZ * 2 });

		createInvisibleWall(Vector3{ 0, containerHalfY / 2, -containerHalfZ - wallThickness / 2 }, Vector3{ containerHalfX * 2, containerHalfY * 2, wallThickness });
		createInvisibleWall(Vector3{ 0, containerHalfY / 2, containerHalfZ + wallThickness / 2 }, Vector3{ containerHalfX * 2, containerHalfY * 2, wallThickness });


		auto ground = _scene.createEntity();
		auto box = _scene.createEntity();

		{
			auto tr = _scene.addComponent<core::components::Transform>(ground);
			tr->position = Vector3{ 0.0f, -1.0f, 0.0f };
			tr->scale = Vector3(10, 1, 10);
			auto collider = _scene.addComponent<physics::BoxCollider>(ground);
			collider->size = Vector3{ 10.0f, 1.0f, 10.0f };
			auto rb = _scene.addComponent<physics::RigidBody>(ground, physics::RigidBody{ physics::BodyType::Static });
			auto cubeRenderer = _scene.addComponent<render::components::MeshRenderer>(ground);
			cubeRenderer->material = matDefault;
			auto cubeMesh = _scene.addComponent<render::components::MeshFilter>(ground);
			cubeMesh->mesh = meshCube;
		}

		{
			//*
			auto textured = _scene.createEntity();

			auto tr = _scene.addComponent<core::components::Transform>(textured);
			tr->rotation = Quaternion::euler(-45, 0, 0);
			tr->scale = Vector3(3, 1, 3);
			auto renderer = _scene.addComponent<render::components::MeshRenderer>(textured);
			renderer->material = render::Primitives::getTextureMaterial();
			auto mat = res.getMaterial(renderer->material);
			mat->setTexture("diffuseTexture", texture2);
			auto mesh = _scene.addComponent<render::components::MeshFilter>(textured);
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
					auto cube = _scene.createEntity();
					auto tr = _scene.addComponent<core::components::Transform>(cube);
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


					auto collider = _scene.addComponent<physics::BoxCollider>(cube);//should be a bug here as physic cant update this data.
					collider->size = scale;
					auto rb = _scene.addComponent<physics::RigidBody>(cube, templateRB);
					auto meshRenderer = _scene.addComponent<render::components::MeshRenderer>(cube);
					meshRenderer->material = matRainbow;
					auto meshFilter = _scene.addComponent<render::components::MeshFilter>(cube);
					meshFilter->mesh = meshCube;
				}
		//*/
		//*
		_bus->subscribe<core::input::InputEvent>(static_cast<uint32_t>(core::input::InputEvent::Type::KeyDown), [&](core::input::InputEvent& event)
			{
				using namespace core::components;
				using namespace physics;
				using namespace render::components;
				if (event.code != static_cast<int>(core::input::KeyCode::Space))
				{
					return;
				}
				auto camServ = ctx.get<core::CameraContextService>();
				auto cameraEntity = camServ->get();
				if (cameraEntity == core::Entity::invalidID) 
				{
					log::warning("No main camera in scene!");
					return;
				}

				auto camTr = _scene.getComponent<Transform>(cameraEntity);
				if (!camTr)
				{
					log::warning("Camera has no Transform!");
					return;
				}
				auto cubeMat = render::Primitives::getDefaultMaterial();
				auto cubeMesh = render::Primitives::getCube();
				auto world = camTr->world();
				Vector3 spawnOffset = world.forward() * 1.0f;
				Vector3 spawnPos = camTr->position + spawnOffset;
				Vector3 shootDir = world.forward();
				Vector3 velocity = shootDir * 15.0f;
				
				auto cube = _scene.createEntity();
				auto tr = _scene.addComponent<Transform>(cube);

				tr->position = spawnPos;
				tr->rotation = camTr->rotation;
				tr->scale = Vector3(0.3f, 0.3f, 0.3f);

				auto collider = _scene.addComponent<BoxCollider>(cube);
				collider->size = Vector3(0.3f, 0.3f, 0.3f);

				RigidBody rb;
				rb.type = BodyType::Dynamic;
				rb.mass = 1.0f;
				rb.linearVelocity = velocity;
				rb.useGravity = true;

				_scene.addComponent<RigidBody>(cube,rb);

				auto meshRenderer = _scene.addComponent<MeshRenderer>(cube);
				meshRenderer->material = cubeMat;
				auto meshFilter = _scene.addComponent<MeshFilter>(cube);
				meshFilter->mesh = cubeMesh;

				log::debug("Cube spawned at {}, {}, {}", spawnPos.x, spawnPos.y, spawnPos.z);

			});
	}
}