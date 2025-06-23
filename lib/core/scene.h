#ifndef __CSYREN_SCENE__
#define __CSYREN_SCENE__

#include <limits>
#include <vector>
#include <unordered_map>

#include "component_base.h"
#include "component_pool.h"

namespace csyren::core
{

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

				it = _updateableComponentPools.find(family);
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

		template<typename T, typename... Args>
		T* addComponent(Entity::ID id, Args&&... args)
		{
			Entity* ent = _entities.get(id);
			if (!ent) return nullptr;

			const size_t family = reflection::ComponentFamily::getID<T>();
			std::unique_ptr<PoolBase>& base =
				reflection::HasUpdate<T> ? _updateableComponentPools[family]
				: _componentPools[family];
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
			Entity* ent = _entities.get(id);
			
			if (!ent) return nullptr;

			const size_t family = reflection::ComponentFamily::getID<T>();
			auto it = ent->components.find(family);
			if (it == ent->components.end()) return nullptr;

			auto& map = reflection::HasUpdate<T> ? _updateableComponentPools : _componentPools;
			auto poolIt = map.find(family);
			if (poolIt == map.end()) return nullptr;
			
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

		cstdmf::PageView<Entity> _entities;
		std::unordered_map<size_t, std::unique_ptr<PoolBase>> _componentPools;
		std::unordered_map<size_t, std::unique_ptr<PoolBase>> _updateableComponentPools;
	};
}

#endif;
