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


namespace csyren::core
{
	Application::Application() :
		_window(1200, 786, L"csyren engine"),
		_inputDispatcher(),
		_bus(std::make_unique<csyren::core::events::EventBus2>()),
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

		auto matHandle = render::Primitives::getDefaultMaterial(_resource);
		auto meshHandle = render::Primitives::getTriangle(_resource);
		
		if (!matHandle || !meshHandle)
			return -1;
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
			
			_inputDispatcher.update();
			_bus->publish(updateToken,updateEvent);

			_render.beginFrame();
			_render.clear(clearColor);
			_bus->publish(drawToken,drawEvent);
			_resource.getMesh(meshHandle)->draw(_render, _resource.getMaterial(matHandle));
			//_scene.draw(_render);//make a part of callback
			_render.endFrame();
			//TODO a fence for thread. flush should work with fixed data
			_scene.flush();
			_bus->commit_batch();
		}
		log::shutdown();
		return static_cast<int>(msg.wParam);
	}


	/**
	 * @brief method where you can place you custom scene initialization.
	 */
	void Application::onSceneStart()
	{
		auto& dispatcher = _inputDispatcher;
		auto& keyboard = dispatcher.devices().keyboard();

		dispatcher.events().subscribe(core::input::InputEvent::Type::KeyUp, [](const core::input::InputEvent& event) {

			std::cout << "up : " << char(event.code) << "\n"; }
		);

		dispatcher.events().subscribe(core::input::InputEvent::Type::MouseButtonDown, [](const core::input::InputEvent& event)
			{
				std::cout << "mouse button pressed:" << event.code << "\n";
			});
		dispatcher.events().subscribe(core::input::InputEvent::Type::MouseMove, [](const core::input::InputEvent& event)
			{
				std::cout << "mouse button move:" << event.data.mouse.x << " " << event.data.mouse.y << "\n";
			});

	}

	
}
