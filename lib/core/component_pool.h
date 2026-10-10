#ifndef  __CSYREN_COMPONENT_POOL__
#define	 __CSYREN_COMPONENT_POOL__
#include "component_base.h"
#include "entity.h"

#include "cstdmf/page_view.h"
#include "cstdmf/sparse_set.h"
#include <unordered_map>

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
        template<class... Args> T* emplace(Entity::ID id, Args&&... args)
        {
            // Reserve identity metadata before publishing a live component. Batch
            // reserveKeys preallocates these entries for its no-throw commit.
            auto [it, inserted] = _generations.try_emplace(id, 0);
            const bool existing = this->contains(id);
            auto* value = cstdmf::SparseSet<T>::emplace(id, std::forward<Args>(args)...);
            if (!existing && value) it->second = ++_nextGeneration;
            return value;
        }
        uint64_t generation(Entity::ID id) const noexcept
        {
            auto it = _generations.find(id);
            return it == _generations.end() ? 0 : it->second;
        }
        void* emplaceDefault(Entity::ID id) override { return this->emplace(id); }
        void* raw(Entity::ID id) noexcept override { return this->try_get(id); }
        std::span<const Entity::ID> keys() const noexcept override { return { this->key_data(), this->size() }; }
        bool canMove() const noexcept override { return transferable; }
        void reserveKeys(std::span<const Entity::ID> keys) override
        {
            this->prepareInsert(keys);
            _generations.reserve(_generations.size() + keys.size());
            for (auto id : keys) _generations.try_emplace(id, 0);
        }
        // Both pools have the same registered family. Destination capacity and
        // sparse pages must have been prepared before this commit phase.
        void moveFrom(PoolBase& source) noexcept override
        {
            if constexpr (transferable) {
                auto& typed = static_cast<ComponentPool<T>&>(source);
                for (auto id : typed.keys()) this->emplace(id, std::move_if_noexcept(*typed.try_get(id)));
            }
        }
    private:
        std::unordered_map<Entity::ID, uint64_t> _generations;
        uint64_t _nextGeneration{};
    };

}


#endif
