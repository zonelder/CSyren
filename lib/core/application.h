#ifndef __CSYREN_APPLICATION__
#define __CSYREN_APPLICATION__

#include "window.h"
#include "scene.h"
#include "renderer.h"


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

	private:
		
		input::InputDispatcher	_inputDispatcher;
		render::Renderer		_render;
		Time					_time;

		Window _window;
		Scene _scene;
	};
}


#endif
