#ifndef  __CSYREN_COMPONENT_POOL__
#define	 __CSYREN_COMPONENT_POOL__
#include "component_base.h"
#include "entity.h"

#include "cstdmf/page_view.h"
#include "cstdmf/sparse_set.h"

namespace csyren::core
{
    struct PoolBase
    {
        virtual ~PoolBase() = default;
    };

    template<class T>
    class ComponentPool : public PoolBase,public cstdmf::SparseSet<T>{};

}


#endif
