#ifndef __CSYREN_APPLICATION__
#define __CSYREN_APPLICATION__

#include "window.h"
#include "scene.h"
#include "renderer.h"
#include "time.h"

#include <string>

namespace csyren::core
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
		
		input::InputDispatcher	_inputDispatcher;
		render::Renderer		_render;
		render::ResourceManager	_resource;
		Time					_time;
		std::unique_ptr<csyren::core::events::EventBus2> _bus;

		Window _window;
		Scene _scene;
	};
}


#endif
