#ifndef __CSYREN_APPLICATION__
#define __CSYREN_APPLICATION__

#include "window.h"
#include "scene.h"

namespace csyren::core
{
	class Application
	{
	public:
		Application();
		void init();

		void game_loop();

	private:
		Time _time;
		render::Renderer _renderer;
	};
}


#endif
