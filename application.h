#pragma once

#include "core/window.h"
#include "core/scene.h"
#include "core/system_manager.h"
#include "core/renderer.h"
#include "core/time.h"

#ifdef DX12_RENDER
#include "dx12_graphic/renderer.h"
#include "dx12_graphic/resource_manager.h"
#endif

#include "physics/physic_system.h"

#include "serializer.h"

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
		std::unique_ptr<core::events::EventBus2>	_bus;
		core::input::InputDispatcher				_inputDispatcher;
		core::Time									_time;
		core::Scene									_scene;
		core::SystemManager							_systems;
		core::Window								_window;

		render::Renderer							_render;
		render::ResourceManager						_resource;

		physics::PhysicsEngine						_physics;

		Serializer									_serializer;
	};
}
