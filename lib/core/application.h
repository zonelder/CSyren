#ifndef __CSYREN_APPLICATION__
#define __CSYREN_APPLICATION__

#include "window.h"
#include "scene.h"
#include "renderer.h"
#include "time.h"

#include <string>

namespace csyren::core
{
	//TODO implement:
	//mesh handling
	//texture handling
	//shader handling
	class ResourceManager{};
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
		Time					_time;
		ResourceManager			_resource;

		Window _window;
		Scene _scene;
	};
}


#endif
