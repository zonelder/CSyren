#include "pch.h"
#include "application.h"
#include "renderer.h"
#include "input_dispatcher.h"
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

// include systems for space shoopter demo;
#include "core/damage_system.h"
#include "core/move_system.h"
#include "core/collision_system.h"
#include "core/mesh_filter.h"
#include "core/player_input_system.h"
#include "core/meshRenderSystem.h"

namespace csyren::core
{
	Application::Application() :
		_window(1200, 786, L"csyren engine"),
		_bus(std::make_unique<csyren::core::events::EventBus2>()),
		_inputDispatcher(*_bus),
		_scene(*_bus),
		_render(),
		_resource(_render)
	{ }

	Application::~Application(){}


	bool Application::init()
	{
		auto hWnd = _window.init();
		if (!hWnd) { return false; }
		_window.setInputDispatcher(&_inputDispatcher);

		if (!_render.init(hWnd, _window.width(), _window.height()))
		{
			return false;
		}

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
		
		auto updateToken = _bus->register_publisher<core::events::UpdateEvent>();
		auto drawToken = _bus->register_publisher<core::events::DrawEvent>();


		auto playerSystem = new csyren::demo::space_shooter::PlayerInputSystem(*_bus, _scene);
		auto collisionSystem = new csyren::demo::space_shooter::CollisionSystem(*_bus);
		auto moveSystem = new csyren::demo::space_shooter::MoveSystem(*_bus);
		auto damageSystem = new csyren::demo::space_shooter::DamageSystem(*_bus);
		auto meshRenderSystem = new csyren::systems::MeshRenderSystem(*_bus);
		onSceneStart();

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
			_bus->publish(updateToken,updateEvent);

			_render.beginFrame();
			_render.clear(clearColor);
			_bus->publish(drawToken,drawEvent);
			_render.endFrame();
			//TODO a fence for thread. flush should work with fixed data
			_scene.flush();
			_bus->commit_batch();
		}
		log::shutdown();


		delete playerSystem;
		delete collisionSystem;
		delete moveSystem;
		delete damageSystem;

		return static_cast<int>(msg.wParam);
	}


	/**
	 * @brief method where you can place you custom scene initialization.
	 */
	void Application::onSceneStart()
	{
		auto& dispatcher = _inputDispatcher;
		auto& keyboard = dispatcher.devices().keyboard();

		//create player and bind move keyboad
		auto player = _scene.createEntity();
		auto playerComponent = _scene.addComponent<PlayerComponent>(player);
		if (!playerComponent)
		{
			log::error("Application::onSceneStart: failed to create player component");
			return;
		}
		auto defaultMaterial = render::Primitives::getDefaultMaterial(_resource);
		if (!defaultMaterial)
		{
			log::error("Application::onSceneStart: failed to get default material");
			return;
		}
		auto circleMesh = render::Primitives::getQuad(_resource);

		if (!circleMesh)
		{
			log::error("Application::onSceneStart: failed to get circle mesh");
			return;
		}

		//create player mesh
		auto playerMesh = _scene.addComponent<components::MeshFilter>(player);
		auto playerMeshRenderer = _scene.addComponent<components::MeshRenderer>(player);
		auto playerTransform = _scene.addComponent<components::Transform>(player);


		playerMesh->mesh = circleMesh;
		playerMeshRenderer->material = defaultMaterial;

		//create a systems 
	}

	
}
