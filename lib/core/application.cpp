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


//

namespace csyren::core
{
	Application::Application() :
		_window(1200, 786, L"csyren engine"),
		_inputDispatcher(),
		_scene(_inputDispatcher)
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
		onSceneStart();
		_window.show();
		MSG msg = { 0 };

		const FLOAT clearColor[4] = { 0.1f, 0.1f, 0.3f, 1.0f };

		render::Material material;
		render::Mesh mesh;
		if (!render::Primitives::createDefaultMaterial(_render, material) || !render::Primitives::createTriangle(_render, mesh))
			return -1;
		while (msg.message != WM_QUIT)
		{
			_window.preMessagePump();
			if (PeekMessage(&msg, 0, 0, 0, PM_REMOVE))
			{
				TranslateMessage(&msg);
				DispatchMessage(&msg);
			}
			_inputDispatcher.update();
			_scene.update(_time);

			_render.beginFrame();
			_render.clear(clearColor);

			mesh.draw(_render, material);
			_scene.draw(_render);

			_render.endFrame();
		}
		log::shutdown();
		return static_cast<int>(msg.wParam);
	}


	/**
	 * @brief method where you can place you custom scene initialization.
	 */
	void Application::onSceneStart()
	{
		auto dispatcher = _scene.input();
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
