#ifndef  __CSYREN_COMPONENT_POOL__
#define	 __CSYREN_COMPONENT_POOL__
#include "component_base.h"
#include "entity.h"

#include "cstdmf/page_view.h"

namespace csyren::core
{
	struct Entity;
    struct PoolBase
    {
        virtual ~PoolBase() = default;
        virtual void update(Scene&, Time&) = 0;
        virtual bool remove(Scene& ,Component::ID) = 0;
        virtual void draw(Scene&, render::Renderer&) = 0;
    };

    template<class T>
    class ComponentPool : public PoolBase
    {
    public:
        struct Record
        {
            Entity::ID owner{ Entity::invalidID };
            T component{};

            Record() = default;

            template<typename... Args>
            Record(Entity::ID e, Args&&... args)
                : owner(e), component(std::forward<Args>(args)...)
            {
            }
        };

        template<typename... Args>
        T* add(Entity::ID ent, Component::ID& outID, Args&&... args)
        {
            auto id = _records.emplace(ent, std::forward<Args>(args)...);
            outID = static_cast<Component::ID>(id);
            return &_records.get(id)->component;
        }

        bool remove(Scene& scene,Component::ID id) override
        {
            if (auto* rec = _records.get(id))
            {
                if constexpr (reflection::HasOnDestroy<T>)
                {
                    rec->component.onDestroy(scene);
                }
               return _records.erase(id);
            }
            return false;
        }

        void update(Scene& scene, Time& time) override
        {
            if constexpr (reflection::HasUpdate<T>)
            {
                for (auto& r : _records)
                {
                    r.component.update(scene, time);
                }
            }

        }

        void draw(Scene& scene,render::Renderer& render) override
        {
            if constexpr (reflection::HasDraw<T>)
            {
                for (auto& r : _records)
                {
                    r.component.draw(scene,render);
                }
            }
        }

        T* get(Component::ID id)
        {
            auto* rec = _records.get(id);
            return rec ? &rec->component : nullptr;
        }
    private:
        cstdmf::PageView<Record> _records;
    };

}


#endif
