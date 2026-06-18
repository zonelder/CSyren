#pragma once


#include "core/window.h"
#include "core/scene.h"
#include "core/system_manager.h"
#include "core/time.h"


#include "physics/physic_system.h"

#include "serializer.h"

#define DX12_RENDER
#ifdef DX12_RENDER
#include "dx12_graphic/forward_decl.h"
#include "dx12_graphic/renderer.h"
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

		void onSceneStart();

	private:
		core::input::InputDispatcher				_inputDispatcher;
		core::SystemManager							_systems;
		physics::PhysicsEngine						_physics;
	};
}
