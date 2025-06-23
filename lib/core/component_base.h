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
	public:
		float deltaTime;
	};

	class Scene;

	class Component
	{
	public:
		using ID = size_t;
		virtual ~Component() = default;
	};
}

#endif;