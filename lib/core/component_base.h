#ifndef __CSYREN_COMPONENT_BASE__
#define __CSYREN_COMPONENT_BASE__

#include <stdint.h>
#include "family_generator.h"

namespace csyren::core::reflection
{
	class ComponentFamilyID {};
	using ComponentFamily = Family<ComponentFamilyID>;

	constexpr size_t MAX_COMPONENT_TYPES = 128;
}


#endif;