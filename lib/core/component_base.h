#ifndef __CSYREN_COMPONENT_BASE__
#define __CSYREN_COMPONENT_BASE__

#include <stdint.h>
#include "family_generator.h"

namespace csyren::render
{
	class Renderer;
}

namespace csyren::core
{
	class Time;

	class Scene;

	class Component
	{
	public:
		using ID = size_t;
		virtual ~Component() = default;
	};
}

namespace csyren::core::reflection
{
	class ComponentFamilyID {};
	using ComponentFamily = Family<ComponentFamilyID>;

	template<typename T>
	concept HasOnCreate = requires(T t, Scene & s) { t.onCreate(s); };

	template<typename T>
	concept HasOnDestroy = requires(T t, Scene & s) { t.onDestroy(s); };

	template<typename T>
	concept HasUpdate = requires(T t, Scene & s, Time & tm) { t.update(s, tm); };

	template<typename T>
	concept HasDraw = requires(T t,Scene& s,render::Renderer& r) { t.draw(s,r); };
}




#endif;