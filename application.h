#pragma once

#include "core/context.h"
#include "core/window.h"
#include "core/scene.h"
#include "core/system_manager.h"
#include "core/time.h"


#include "physics/physic_system.h"

#include "serializer.h"

#define DX12_RENDER
#ifdef DX12_RENDER
#include "dx12_graphic/renderer.h"
#include "dx12_graphic/resource_manager.h"
#else
static_assert(false && "none render pipeline was added.\n");
#endif

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

		void onSceneStart(core::ServiceContext& ctx);

	private:
		std::unique_ptr<core::events::EventBus2>	_bus;
		core::input::InputDispatcher				_inputDispatcher;
		core::Scene									_scene;
		core::SystemManager							_systems;
		core::Window								_window;

		render::Renderer							_render;
		render::ResourceManager						_resource;

		physics::PhysicsEngine						_physics;

		Serializer									_serializer;
	};
}
