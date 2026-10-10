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
        virtual void* emplaceDefault(Entity::ID) = 0;
        virtual void* raw(Entity::ID) noexcept = 0;
        virtual std::span<const Entity::ID> keys() const noexcept = 0;
        virtual bool canMove() const noexcept = 0;
        virtual void reserveKeys(std::span<const Entity::ID>) = 0;
        virtual void moveFrom(PoolBase&) noexcept = 0;
    };

    template<class T>
    class ComponentPool : public PoolBase, public cstdmf::SparseSet<T>
    {
        static constexpr bool transferable = std::is_nothrow_move_constructible_v<T> || std::is_nothrow_copy_constructible_v<T>;
    public:
        void* emplaceDefault(Entity::ID id) override { return this->emplace(id); }
        void* raw(Entity::ID id) noexcept override { return this->try_get(id); }
        std::span<const Entity::ID> keys() const noexcept override { return { this->key_data(), this->size() }; }
        bool canMove() const noexcept override { return transferable; }
        void reserveKeys(std::span<const Entity::ID> keys) override { this->prepareInsert(keys); }
        // Both pools have the same registered family. Destination capacity and
        // sparse pages must have been prepared before this commit phase.
        void moveFrom(PoolBase& source) noexcept override
        {
            if constexpr (transferable) {
                auto& typed = static_cast<ComponentPool<T>&>(source);
                for (auto id : typed.keys()) this->emplace(id, std::move_if_noexcept(*typed.try_get(id)));
            }
        }
    };

}


#endif
