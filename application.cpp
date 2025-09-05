#include "application.h"

#include "cstdmf/log.h"
#include <cassert>
#include <format>
#include <iostream>

//
#include "dx12_graphic/primitives.h"
#include "dx12_graphic/mesh.h"
#include "dx12_graphic/material.h"

#include "core/event_bus.h"
#include "core/context.h"
#include "core/time.h"
#include "core/camera.h"
#include "core/input_dispatcher.h"

#include "mesh_render_system.h"
#include "editor_camera_controller_system.h"
#include "scene_loader.h"

#include "math/math.h"

namespace
{


	DirectX::XMMATRIX createProjection(csyren::core::Camera& camera)
	{

		using namespace DirectX;
		using namespace csyren::core;

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

		auto hWnd = _window.init();
		if (!hWnd) { return false; }
		_window.setInputDispatcher(&_inputDispatcher);

		if (!_render.init(hWnd, _window.width(), _window.height()))
		{
			return false;
		}

		_inputDispatcher.init(*_bus);
		return true;
	}

	int Application::run()
	{
		log::init();
		_window.show();
		MSG msg = { 0 };

		const FLOAT clearColor[4] = { 0.1f, 0.1f, 0.3f, 1.0f };

		core::Time time;
		core::details::TimeHandler timeHandler;

		core::events::UpdateEvent updateEvent{ _inputDispatcher.devices(), _scene,_resource,*_bus,time			};
		core::events::DrawEvent   drawEvent  { _inputDispatcher.devices(), _scene,_resource,*_bus,_render		};
		core::events::SystemEvent systemEvent{ _inputDispatcher.devices(), _scene,_resource,*_bus,time,_render  };

		onSceneStart();

		_systems.init(systemEvent);
		while (true)
		{
			timeHandler.update(time);
			_window.preMessagePump();
			while (PeekMessage(&msg, 0, 0, 0, PM_REMOVE))
			{
				if (msg.message == WM_QUIT) 
				{
					_systems.shutdown(systemEvent);
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

			auto perFrameBuffer = _render.getPerFrameBuffer();
			perFrameBuffer->invView = cameraTransform.world();
			perFrameBuffer->view = DirectX::XMMatrixInverse(nullptr, perFrameBuffer->invView);
			perFrameBuffer->projection = createProjection(camera);
			perFrameBuffer->viewProjection = perFrameBuffer->view* perFrameBuffer->projection;
			auto perFrameCB = _render.getPerFrameCB();
			perFrameCB->update(perFrameBuffer, sizeof(render::PerFrameBuffer));

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

		auto mainCameraEntt = _scene.createEntity();
		auto mainCamera = _scene.addComponent<Camera>(mainCameraEntt);
		auto cameraTransform = _scene.addComponent<Transform>(mainCameraEntt);
		auto editorCameraController = _scene.addComponent<EditorCameraController>(mainCameraEntt);
		editorCameraController->movementSpeed = 1.0f;
		mainCamera->aspectRatio = _window.width() / _window.height();
		mainCamera->background = { 1.f,0.0f,0.0f,1.0f };
		cameraTransform->position = math::Vector3::back * 2;
		
		auto matHandle = render::Primitives::getDefaultMaterial(_resource);
		auto meshHandle = render::Primitives::getTriangle(_resource);

		//-----------------------------init systems---------------------------------------------------
		//
		//--------------------------------------------------------------------------------------------

		auto sceneLoaderSystem = std::make_shared<csyren::SceneLoaderSystem>(_serializer);
		auto editorCameraControllerSystem = std::make_shared<csyren::EditorCameraControllerSystem>();
		auto meshRenderSystem = std::make_shared<csyren::MeshRenderSystem>();

		_systems.addSystem(sceneLoaderSystem, -100); 
		_systems.addSystem(editorCameraControllerSystem, -1);
		_systems.addSystem(meshRenderSystem, 0);

		//---------------------------------------------------------------------------------------------

		auto testMeshEntity = _scene.createEntity();
		auto meshFilter = _scene.addComponent<MeshFilter>(testMeshEntity);
		auto meshRenderer = _scene.addComponent<MeshRenderer>(testMeshEntity);
		auto transform = _scene.addComponent<Transform>(testMeshEntity);
		meshFilter->mesh = meshHandle;
		meshRenderer->material = matHandle;

		auto saveComponent = _scene.createEntity();
		auto saveReq = _scene.addComponent<core::SceneLoaderRequest>(saveComponent);
		saveReq->type = core::SceneLoaderRequest::SAVE;
		saveReq->path = "E:\\test_scene.scene";



	}
}