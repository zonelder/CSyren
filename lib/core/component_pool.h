#ifndef  __CSYREN_COMPONENT_POOL__
#define	 __CSYREN_COMPONENT_POOL__
#include "component_base.h"
#include "entity.h"
#include <unordered_map>

#include "cstdmf/page_view.h"
#include "cstdmf/sparse_set.h"

namespace csyren::core
{
    struct PoolBase
    {
        virtual ~PoolBase() = default;
    };

    template<class T>
    class ComponentPool : public PoolBase, public cstdmf::SparseSet<T> {
        using Storage = cstdmf::SparseSet<T>;
    public:
        template<class... Args> T* emplace(Entity::ID id, Args&&... args) {
            auto* result = Storage::emplace(id, std::forward<Args>(args)...);
            if (result) _versions[id] = ++_nextVersion;
            return result;
        }
        void erase(Entity::ID id) { Storage::erase(id); _versions.erase(id); }
        uint64_t version(Entity::ID id) const {
            auto it = _versions.find(id);
            return it == _versions.end() ? 0 : it->second;
        }
    private:
        std::unordered_map<Entity::ID, uint64_t> _versions;
        uint64_t _nextVersion = 0;
    };

}


#endif
