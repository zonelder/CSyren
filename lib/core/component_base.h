#ifndef __CSYREN_COMPONENT_BASE__
#define __CSYREN_COMPONENT_BASE__

#include <stdint.h>
#include "family_generator.h"

namespace csyren::core::reflection
{
	class ComponentFamilyID {};
	using ComponentFamily = Family<ComponentFamilyID>;
}


namespace csyren::core
{
	class Time
	{
		float deltaTime;
	};

	class Scene;

	class Component
	{
	public:
		using ID = uint32_t;
		virtual ~Component() = default;
		virtual void onCreate(Scene&) {}//DEVIRTUALIZE
		virtual void onDestroy(Scene&) {}//DEVIRTUALIZE
		virtual void update(Scene&, Time&) {}//DEVIRTUALIZE
	};
}

#endif;