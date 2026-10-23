#ifndef __CSYREN_COMPONENT_BASE__
#define __CSYREN_COMPONENT_BASE__

#include <stdint.h>
#include "family_generator.h"

namespace csyren::core::reflection
{
	class ComponentFamilyID {};
	// Family ids are handed out per reflected type (components and other reflected
	// structs); there is no upper limit.
	using ComponentFamily = Family<ComponentFamilyID>;
}


#endif;