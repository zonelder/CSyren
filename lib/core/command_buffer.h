#ifndef __CSYREN_COMMAND_BUFFER__
#define __CSYREN_COMMAND_BUFFER__

#include "entity.h"
#include "component_base.h"

namespace csyren::core
{
    struct DestroyComponentCommand
    {
        Entity::ID   entt;
        size_t       family;
    };
}


#endif

