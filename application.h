#pragma once

#include "core/window.h"
#include "core/scene.h"
#include "core/system_manager.h"
#include "core/renderer.h"
#include "core/time.h"
#include "core/camera.h"
#include "core/serializer.h"

namespace csyren
{


	class Application
	{
	public:
		Application();
		~Application();

		Application(const Application& rhs) = delete;
		Application& operator=(const Application& rhs) = delete;

		bool init();
		int	 run();

		void onSceneStart();

	private:
		core::input::InputDispatcher	_inputDispatcher;
		render::Renderer		_render;
		render::ResourceManager	_resource;
		core::Time					_time;
		std::unique_ptr<core::events::EventBus2> _bus;

		core::Window _window;
		core::Scene _scene;
		core::SystemManager _systems;
		core::Serializer	_serializer;
	};
}
