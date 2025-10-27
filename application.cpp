#include "application.h"

#include "cstdmf/log.h"
#include <cassert>
#include <format>
#include <iostream>

#include "dx12_graphic/primitives.h"
#include "dx12_graphic/mesh.h"
#include "dx12_graphic/material.h"

#include "core/event_bus.h"
#include "core/context.h"
#include "core/time.h"
#include "core/camera.h"
#include "core/transform.h"
#include "core/input_dispatcher.h"

#include "mesh_render_system.h"
#include "editor_camera_controller_system.h"
#include "debug_rotator_system.h"
#include "scene_loader.h"

#include "math/math.h"

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
}

namespace csyren
{
	Application::Application() :
		_window(1200, 786, L"csyren engine"),
		_inputDispatcher(),
		_bus(std::make_unique<csyren::core::events::EventBus2>()),
		_scene(*_bus),
		_render(),
		_resource(_render),
		_serializer(_scene,_resource)
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

		if (!_render.init(hWnd, _window.width(), _window.height()))
		{
			return false;
		}

		_inputDispatcher.init(*_bus);
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

		core::events::UpdateEvent updateEvent{ _inputDispatcher.devices(), _scene,_resource,*_bus,time			};
		core::events::DrawEvent   drawEvent  { _inputDispatcher.devices(), _scene,_resource,*_bus,_render		};
		core::events::SystemEvent systemEvent{ _inputDispatcher.devices(), _scene,_resource,*_bus,time,_render  };

		log::info("-------------------------------Setup Start Up------------------------------------------------");
		_render.beginResourceUpload();
		_physics.initialize(systemEvent);
		render::Primitives::registerFabricsAll(_resource);
		onSceneStart();
		_systems.init(systemEvent);

		_render.endResourceUpload();
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
					_systems.shutdown(systemEvent);
					_physics.shutdown(systemEvent);
					_inputDispatcher.shutdown(*_bus);
					log::shutdown();
					return static_cast<int>(msg.wParam);
				}
				TranslateMessage(&msg);
				DispatchMessage(&msg);
			}

			_inputDispatcher.update(*_bus);
			_systems.update(updateEvent);
			auto [mainCameraID,camera,cameraTransform] = *(_scene.view<Camera,Transform>().begin());//only first camera accepted

			auto engineVariables = _render.getEngineVariableBuffer();
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

			_render.beginFrame();
			_render.clear(&(camera.background.x));

			_systems.draw(drawEvent);

			_render.endFrame();

			_scene.flush();
			_bus->commit_batch();
		}
	}


	/**
	 * @brief method where you can place you custom scene initialization.
	 */
	void Application::onSceneStart()
	{
		using namespace core::components;
		using namespace render::components;
		//-----------------------------init systems---------------------------------------------------
		//
		//--------------------------------------------------------------------------------------------
		auto sceneLoaderSystem = std::make_shared<csyren::SceneLoaderSystem>(_serializer);
		auto editorCameraControllerSystem = std::make_shared<csyren::EditorCameraControllerSystem>();
		auto debugRotatorSystem = std::make_shared<csyren::DebugRotatorSystem>();
		auto meshRenderSystem = std::make_shared<csyren::MeshRenderSystem>();

		_systems.addSystem(sceneLoaderSystem, -100); 
		_systems.addSystem(debugRotatorSystem, -2);
		_systems.addSystem(editorCameraControllerSystem, -1);
		_systems.addSystem(meshRenderSystem, 0);

		//------------------------------------LOAD SCENE------------------------------------------------

		//------------------------------------Camera----------------------------------------------------
		auto mainCameraEntt = _scene.createEntity();
		auto mainCamera = _scene.addComponent<Camera>(mainCameraEntt);
		auto cameraTransform = _scene.addComponent<Transform>(mainCameraEntt);
		auto editorCameraController = _scene.addComponent<EditorCameraController>(mainCameraEntt);
		editorCameraController->movementSpeed = 1.0f;
		mainCamera->aspectRatio = _window.width() / _window.height();
		mainCamera->background = { 1.f,0.0f,0.0f,1.0f };
		cameraTransform->position = math::Vector3::back * 2;
		
		//-------------------------------------Material and mesh----------------------------------------

		auto matDefault = render::Primitives::getDefaultMaterial(_resource);
		auto matRainbow = render::Primitives::getRainbowMaterial(_resource);
		_resource.getMaterial(matDefault)->setVector("tint", DirectX::XMFLOAT4(1, 1, 1,1));
		_resource.getMaterial(matRainbow)->setVector("tint", DirectX::XMFLOAT4(1, 1, 1, 1));
		auto meshQuad = render::Primitives::getQuad(_resource);
		auto meshCube = render::Primitives::getCube(_resource);

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

		//---------------------------------------------------------------------------------------------
		/*
		auto saveComponent = _scene.createEntity();
		auto saveReq = _scene.addComponent<core::SceneLoaderRequest>(saveComponent);
		saveReq->type = core::SceneLoaderRequest::LOAD;
		saveReq->path = "E:\\test_scene.scene";
		*/
	}
}