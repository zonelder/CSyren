#ifndef __CSYREN_SCENE__
#define __CSYREN_SCENE__

#include <limits>
#include <vector>
#include <unordered_map>

#include "component_base.h"
#include "component_pool.h"
#include "component_order.h"
#include "renderer.h"
#include "input_dispatcher.h"

namespace csyren::core
{

	//todo: make scene the sender of the global events
	// OnCreateEvent<T>
	// OnDestroyEvent<T>
	// OnDisable<T>
	// OnEnable<T>
	// UpdateEvent
	// DrawEvent
	// DrawGizmoEvent
	// EntityCreateEvent
	// EntityDestroyEvent



	struct PoolHandler
	{
		using PoolMap = std::unordered_map<size_t, std::shared_ptr<PoolBase>>;

		struct OrderedPools
		{
			PoolMap natural;
			PoolMap hierarchy;
		};
	public:

		void update(Scene& scene, Time& time)
		{
			for (auto& [family, pool] : _updatablePools.natural)
			{
				pool->update(scene, time);
			}

			for (auto& [family, pool] : _updatablePools.hierarchy)
			{
				pool->update(scene, time);
			}
		}

		void draw(Scene& scene,render::Renderer& render)
		{
			for (auto& [family, pool] : _drawablePools.natural)
			{
				pool->draw(scene,render);
			}

			for (auto& [family, pool] : _drawablePools.hierarchy)
			{
				pool->draw(scene,render);
			}
		}

		template<typename T, typename... Args>
		std::pair<T*, Component::ID> add(Entity::ID entt_id,Args&&... args)
		{
			const size_t family = reflection::ComponentFamily::getID<T>();
			auto pool = getOrCreatePool<T>(family);
			Component::ID id;
			return { pool->add(entt_id,id, std::forward<Args>(args)...),id };
		}

		template<typename T>
		bool remove(Scene& scene,Component::ID id)
		{
			if (auto pool = getPool<T>())
			{
				return pool->remove(scene,id);
			}
			return false;
		}

		bool remove(Scene& scene,size_t family, Component::ID id)
		{
			if (auto it = allPools.find(family); it != allPools.end())
			{
				return it->second->remove(scene,id);
			}
			return false;
		}

		template<typename T>
		T* get(Component::ID id)
		{
			if (auto pool = getPool<T>())
			{
				return pool->get(id);
			}
			return nullptr;
		}
	private:
		template<typename T>
		std::shared_ptr<ComponentPool<T>> getOrCreatePool(size_t family) {
			// Try to find existing pool
			if (auto pool = getPool<T>()) return pool;

			// Create new pool
			auto newPool = std::make_shared<ComponentPool<T>>();
			allPools[family] = newPool;

			// Add to appropriate ordered groups
			using Order = reflection::order::default_order<T>;

			if constexpr (reflection::HasUpdate<T>)
			{
				if constexpr (std::is_same_v<Order, reflection::order::natural_order_t>)
				{
					_updatablePools.natural[family] = newPool;
				}
				else if constexpr (std::is_same_v<Order, reflection::order::hierarchy_order_t>)
				{
					_updatablePools.hierarchy[family] = newPool;
				}
			}

			if constexpr (reflection::HasDraw<T>)
			{
				if constexpr (std::is_same_v<Order, reflection::order::natural_order_t>)
				{
					_drawablePools.natural[family] = newPool;
				}
				else if constexpr (std::is_same_v<Order, reflection::order::hierarchy_order_t>)
				{
					_drawablePools.hierarchy[family] = newPool;
				}
			}

			return newPool;
		}

		template<typename T>
		std::shared_ptr<ComponentPool<T>> getPool()
		{
			const size_t family = reflection::ComponentFamily::getID<T>();
			if (auto it = allPools.find(family); it != allPools.end())
			{
				return std::static_pointer_cast<ComponentPool<T>>(it->second);
			}
			return nullptr;
		}

	private:
		PoolMap allPools;
		OrderedPools _updatablePools;
		OrderedPools _drawablePools;
	};

	class Scene
	{
	public:
		explicit Scene() = default;

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
				_handler.remove(*this,family, compId);
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

			auto [comp, compID] = _handler.add<T>(id,std::forward<Args>(args)...);
			ent->components[reflection::ComponentFamily::getID<T>()] = compID;
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

			bool wasRemoved = _handler.remove<T>(*this,it->second);
		}

		template<typename T>
		T* getComponent(Entity::ID id)
		{
			Entity* ent = _entities.get(id);
			
			if (!ent) return nullptr;

			const size_t family = reflection::ComponentFamily::getID<T>();
			auto it = ent->components.find(family);
			if (it == ent->components.end()) return nullptr;

			return _handler.get<T>(it->second);
		}

		void update(Time& time)
		{
			_handler.update(*this, time);
		}

		void draw(render::Renderer& render)
		{
			_handler.draw(*this, render);
		}


	private:
		cstdmf::PageView<Entity> _entities;
		PoolHandler _handler;
	};

}

#endif;
