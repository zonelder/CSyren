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
#include "core/input_dispatcher.h"

#include "mesh_render_system.h"


namespace csyren
{
	Application::Application() :
		_window(1200, 786, L"csyren engine"),
		_inputDispatcher(),
		_bus(std::make_unique<csyren::core::events::EventBus2>()),
		_scene(*_bus),
		_render(),
		_resource(_render)
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

		core::events::UpdateEvent updateEvent{ _scene,_resource,*_bus,time };
		core::events::DrawEvent   drawEvent{ _scene,_resource,*_bus,_render };
		core::events::SystemEvent systemEvent{ _scene,_resource,*_bus,time,_render };

		onSceneStart();

		_systems.init(systemEvent);

		while (msg.message != WM_QUIT)
		{
			timeHandler.update(time);
			_window.preMessagePump();
			if (PeekMessage(&msg, 0, 0, 0, PM_REMOVE))
			{
				TranslateMessage(&msg);
				DispatchMessage(&msg);
			}

			_inputDispatcher.update(*_bus);
			_systems.update(updateEvent);

			_render.beginFrame();
			_render.clear(clearColor);
			_systems.draw(drawEvent);
			//_resource.getMesh(meshHandle)->draw(_render, _resource.getMaterial(matHandle));
			_render.endFrame();
			_scene.flush();
			_bus->commit_batch();
		}
		_systems.shutdown(systemEvent);
		_inputDispatcher.shutdown(*_bus);
		log::shutdown();
		return static_cast<int>(msg.wParam);
	}

	/**
	 * @brief method where you can place you custom scene initialization.
	 */
	void Application::onSceneStart()
	{
		auto matHandle = render::Primitives::getDefaultMaterial(_resource);
		auto meshHandle = render::Primitives::getTriangle(_resource);

		_bus->subscribe<core::input::InputEvent>(static_cast<core::events::EventMarker>(core::input::InputEvent::Type::MouseMove), [](core::input::InputEvent& event)
			{
				std::cout << "mouse move:" << event.data.mouse.x << " " << event.data.mouse.y << "\n";
			});

		auto meshRenderSystem = std::make_shared<csyren::MeshRenderSystem>();
		_systems.addSystem(meshRenderSystem, 0);

		auto testMeshEntity = _scene.createEntity();
		auto meshFilter = _scene.addComponent<MeshFilter>(testMeshEntity);
		auto meshRenderer = _scene.addComponent<MeshRenderer>(testMeshEntity);
		auto transform = _scene.addComponent<Transform>(testMeshEntity);
		meshFilter->mesh = meshHandle;
		meshRenderer->material = matHandle;
	}


}