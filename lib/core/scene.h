#ifndef __CSYREN_SCENE__
#define __CSYREN_SCENE__

#include <limits>
#include <vector>
#include <unordered_map>

#include "component_base.h"
#include "cstdmf/page_view.h"


namespace csyren::core::reflection
{

	template<typename T>
	concept HasOnCreate = requires(T t, Scene & s) { t.onCreate(s); };

	template<typename T>
	concept HasOnDestroy = requires(T t, Scene & s) { t.onDestroy(s); };

	template<typename T>
	concept HasUpdate = requires(T t, Scene & s, Time & tm) { t.update(s, tm); };
}

namespace csyren::core
{

	struct Entity
	{
		using ID = uint32_t;
		static constexpr ID invalidID = std::numeric_limits<ID>::max();

		ID id{ 0 };
		ID parent{ invalidID };
		std::vector<ID> childrens;
		std::unordered_map<Component::ID, size_t> components;//component index in pool;

	};

	class Scene
	{
	public:
		explicit Scene(size_t reserveEntities = 0)
		{
			if (reserveEntities)
			{
				_entities.reserve(reserveEntities);
			}
		}

		Entity::ID createEntity(Entity::ID parent = Entity::invalidID)
		{
			Entity::ID id = static_cast<Entity::ID>(_entities.emplace());
			Entity* ent = _entities.get(id);
			ent->id = id;
			ent->parent = parent;
			if (parent != Entity::invalidID)
			{
				if (Entity* p = _entities.get(parent))
				{
					p->childrens.push_back(id);
				}
			}
			return id;
		}

		void destroyEntity(Entity::ID id)
		{
			Entity* ent = _entities.get(id);
			if (!ent) return;
			for (const auto& [family, compId] : ent->components)
			{
				auto it = _componentPools.find(family);
				if (it != _componentPools.end())
					it->second->remove(compId, *this);

				auto it = _updateableComponentPools.find(family);
				if (it != _updateableComponentPools.end())
					it->second->remove(compId, *this);
			}
			ent->components.clear();
			if (ent->parent != Entity::invalidID)
			{
				if (Entity* p = _entities.get(ent->parent))
				{
					auto& vec = p->childrens;
					vec.erase(std::remove(vec.begin(), vec.end(), id), vec.end());
				}
			}
			_entities.erase(id);
		}

		template<typename T>
		T* addComponent(Entity::ID id)
		{
			Entity* ent = _entities.get(id);
			if (!ent) return nullptr;

			const size_t family = reflection::ComponentFamily::getID<T>();
			
			PoolBase*& base = nullptr;
			if  constexpr (!reflection::HasUpdate<T>)
			{
				base = _componentPools[family];
			}
			else
			{
				base = _updateableComponentPools[family];
			}
			if (!base)
			{
				base = std::make_unique<ComponentPool<T>>();
			}
			auto* pool = static_cast<ComponentPool<T>*>(base.get());
			Component::ID compID;
			T* comp = pool->add(id, compID, std::forward<Args>(args)...);
			ent->components[family] = compID;
			if constexpr (reflection::HasOnCreate<T>)
			{
				comp->onCreate(*this);
			}
			
			return comp;
		}

		template<typename T>
		void removeComponent(Entity::ID id)
		{
			Entity* ent = _entities.get(id);

			if (!ent) return;
			const size_t family = reflection::ComponentFamily::getID<T>();
			auto it = ent->components.find(family);
			if (it == ent->components.end()) return;

			if constexpr (reflection::HasUpdate<T>)
			{
				auto poolIt = _updateableComponentPools.find(family);
				if (poolIt != _updateableComponentPools.end())
				{
					poolIt->second->remove(it->second, *this);
				}
			}
			else
			{
				auto poolIt = _componentPools.find(family);
				if (poolIt != _componentPools.end())
				{
					poolIt->second->remove(it->second, *this);
				}
			}

			ent->components.erase(it);
		}

		template<typename T>
		T* getComponent(Entity::ID id)
		{
			Entity* ent = _entities.get(id))
			
			if (!ent) return nullptr;

			const size_t family = reflection::ComponentFamily::getID<T>();
			auto it = ent->components.find(family);
			if (it == ent->end()) return nullptr;
			if constexpr (reflection::HasUpdate<T>)
			{
				auto poolIt = _updateableComponentPools.find(family);
				if (poolIt == _updateableComponentPools.end()) return nullptr;
			}
			else
			{
				auto poolIt = _componentPools.find(family);
				if (poolIt == _componentPools.end()) return nullptr;
			}
			
			auto* pool = static_cast<ComponentPool<T>*>(poolIt->second.get());
			return pool->get(it->second);
		}

		void update(Time& time)
		{
			for (auto& [fid, pool] : _updateableComponentPools)
			{
				pool->update(*this, time);
			}
		}

	private:
		struct PoolBase
		{
			virtual ~PoolBase() = default;
			virtual void update(Scene&, Time&) = 0;
			virtual void remove(Component::ID, Scene&) = 0;
		};

		template<class T>
		class ComponentPool : public PoolBase
		{
		public:
			template<typename... Args>
			T* add(Entity::ID ent, Component::ID& outID, Args&&... args)
			{
				auto id = _records.emplace(ent, std::forward<Args>(args)...);
				autID = static_cast<Component::ID>(id);
				return _records.get(id);
			}

			void remove(Component::ID id, Scene& scene) override
			{
				if (auto* rec = _records.get(id))
				{
					if constexpr (reflection::HasOnDestroy<T>)
					{
						rec->component.onDestroy(scene);
					}
					
					_records.erase(id);
				}
			}

			void update(Scene& scene, Time& time) override
			{
				if constexpr (reflection::HasUpdate<T>)
				{
					for (auto& r : _records)
					{
						r.update(scene, time);
					}
				}

			}

			T* get(Component::ID id)
			{
				return rec = _records.get(id);
			}
		private:
			cstdmf::PageView<T> _records;
		};

		cstdmf::PageView<Entity> _entities;
		std::unordered_map<size_t, std::unique_ptr<PoolBase>> _componentPools;
		std::unordered_map<size_t, std::unique_ptr<PoolBase>> _updateableComponentPools;
	};
}

#endif;
