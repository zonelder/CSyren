#ifndef __CSYREN_SCENE__
#define __CSYREN_SCENE__

#include <limits>
#include <vector>
#include <unordered_map>
#include <variant>

#include "component_base.h"
#include "component_pool.h"
#include "component_order.h"
#include "renderer.h"
#include "input_dispatcher.h"

#include "command_buffer.h"
#include "event_bus.h"

class SceneTest;

namespace csyren::core::events
{
	struct EntityCreateEvent { Entity::ID id; };
	struct EntityDestroyEvent { Entity::ID id; };

	template<typename T>
	struct ComponentCreateEvent
	{
		Entity::ID   entity;
		T* ptr;
	};

	template<typename T>
	struct ComponentDestroyEvent
	{
		Entity::ID   entity;
		T* ptr;
	};
}

namespace csyren::core
{
	template<typename... Cs>
	class SceneView;

	class Application;


	class Scene
	{
		friend class Application;
		friend SceneTest;
		template<typename...> friend class SceneView;

		using DestructFn = bool(Scene*,const DestroyComponentCommand&,events::PublishToken,events::EventBus2&);
		struct ComponentMeta
		{
			std::shared_ptr<PoolBase> pool;
			events::PublishToken      addToken;
			events::PublishToken      removeToken;
			DestructFn* removeFn = nullptr;
		};
		using ComponentsMeta = std::unordered_map<size_t, ComponentMeta>;

		template<typename T>
		struct ComponentOps
		{
			
			static bool destroyThunk(Scene* self,const DestroyComponentCommand& c,events::PublishToken token,events::EventBus2& bus)
			{
				auto pool = self->getPool<T>();
				T* ptr = pool ? pool->try_get(c.entt) : nullptr;
				if (ptr)
				{
					bus.publish(token, events::ComponentDestroyEvent<T>{c.entt, ptr});
					pool->erase(c.entt);
					if (Entity* ent = self->_entities.try_get(c.entt))
					{
						ent->components.set(reflection::ComponentFamily::getID<T>(), false);
					}
				}
				return false;
			}
		};
	public:
		explicit Scene(events::EventBus2& bus) :_bus(bus)
		{
			_entityCreateToken = _bus.register_publisher<events::EntityCreateEvent>();
			_entityDestroyToken = _bus.register_publisher<events::EntityDestroyEvent>();
		};

		[[nodiscard]] Entity::ID createEntity(Entity::ID parent = Entity::invalidID)
		{
			Entity::ID id;
			if (!_freeIDs.empty())
			{
				id = _freeIDs.back();
				_freeIDs.pop_back();
			}
			else
			{
				if (_nextId == Entity::invalidID)
				{
					throw std::runtime_error("Scene: out of Entity IDs");
				}
				id = _nextId++;
			}
			_entities.emplace(id, Entity{});
			Entity* ent = _entities.try_get(id);
			ent->id = id;
			ent->parent = parent;
			if (parent != Entity::invalidID)
			{
				if (Entity* p = _entities.try_get(parent))
				{
					p->childrens.push_back(id);
				}
			}
			_bus.publish(_entityCreateToken, events::EntityCreateEvent{id});
			return id;
		}

		void destroyEntity(Entity::ID id)
		{
			Entity* ent = _entities.try_get(id);
			if (!ent) return;

			for (Entity::ID child : ent->childrens)
				destroyEntity(child);

			_deferred.pushDestroyEntity(id);
		}

		template<typename T, typename... Args>
		[[nodiscard]] T* addComponent(Entity::ID id, Args&&... args)
		{
			Entity* ent = _entities.try_get(id);
			if (!ent) return nullptr;

			const size_t family = reflection::ComponentFamily::getID<T>();
			if (ent->components.test(family)) throw std::runtime_error("Component Already presented)");

			auto pool = getOrCreatePool<T>(family);
			T* ptr = pool->emplace(id, std::forward<Args>(args)...);
			if (ptr)
			{
				ent->components[family] = true;
				_bus.publish(getAddToken<T>(),
					events::ComponentCreateEvent<T>{id, ptr});
			}
			return ptr;
		}

		template<typename T>
		void removeComponent(Entity::ID id)
		{
			Entity* ent = _entities.try_get(id);
			if (!ent) return;

			const size_t family = reflection::ComponentFamily::getID<T>();
			if (!ent->components.test(family)) return;
			_deferred.pushDestroyComponent(id, family);
		}

		void removeComponent(Entity::ID id, size_t family)
		{
			Entity* ent = _entities.try_get(id);
			if (!ent) return;
			if (!ent->components.test(family)) return;
			_deferred.pushDestroyComponent(id, family);
		}

		template<typename T>
		T* getComponent(Entity::ID id)
		{
			if (!_entities.contains(id)) return nullptr;
			return getPool<T>() ? getPool<T>()->try_get(id) : nullptr;
		}

		template<typename... Cs>
		SceneView<Cs...> view(){ return SceneView<Cs...>(*this); }

		const cstdmf::SparseSet<Entity>& entities() const { return _entities; }

	private:
		template<typename T>
		std::shared_ptr<ComponentPool<T>> getPool()
		{
			size_t family = reflection::ComponentFamily::getID<T>();
			auto it = _meta.find(family);
			if (it != _meta.end())
				return std::static_pointer_cast<ComponentPool<T>>(it->second.pool);
			return nullptr;
		}
		template<typename T>
		std::shared_ptr<ComponentPool<T>> getOrCreatePool(size_t family)
		{
			if (auto pool = getPool<T>()) return pool;

			auto newPool = std::make_shared<ComponentPool<T>>();
			_meta[family].pool = newPool;
			registerOps<T>();
			return newPool;
		}

		template<typename T>
		void registerOps()
		{
			const size_t family = reflection::ComponentFamily::getID<T>();
			ComponentMeta& m = _meta[family];
			m.addToken = _bus.register_publisher<events::ComponentCreateEvent<T>>();
			m.removeToken = _bus.register_publisher<events::ComponentDestroyEvent<T>>();
			m.removeFn = &ComponentOps<T>::destroyThunk;
		}
		template<typename T>
		events::PublishToken& getAddToken()
		{
			size_t family = reflection::ComponentFamily::getID<T>();
			if (!_meta.contains(family)) getOrCreatePool<T>(family);
			return _meta[family].addToken;
		}

		template<typename T>
		events::PublishToken& getRemoveToken()
		{
			size_t family = reflection::ComponentFamily::getID<T>();
			if (!_meta.contains(family)) getOrCreatePool<T>(family);
			return _meta[family].removeToken;
		}

		void flush()
		{
			for (const auto& e : _deferred.destroyComponentBuf())
			{
				auto it = _meta.find(e.family);
				if (it == _meta.end())
					continue;
				it->second.removeFn(this, e,it->second.removeToken,_bus);
			}

			DestroyComponentCommand cm;
			for (const auto& e : _deferred.destroyEntityBuf())
			{

				Entity* ent = _entities.try_get(e.id);
				if (!ent) continue;
				for (const auto& [family,m] : _meta)
				{
					if (ent->components.test(family))
					{
						cm.entt = e.id;
						cm.family = family;
						m.removeFn(this, cm, m.removeToken, _bus);
					}
				}

				_bus.publish(_entityDestroyToken, events::EntityDestroyEvent{ e.id });

				if (ent->parent != Entity::invalidID)
				{
					if (Entity* p = _entities.try_get(ent->parent))
					{
						auto& vec = p->childrens;
						vec.erase(std::remove(vec.begin(), vec.end(), e.id), vec.end());
					}
				}

				for (auto& child : ent->childrens)
				{
					Entity* pChild = _entities.try_get(child);
					if (!pChild)
					{
						log::error("destroyEntity: child dont exist but entity keep reference to it.");
						continue;
					}
					pChild->parent = Entity::invalidID;
				}
				_entities.erase(e.id);
				if (e.id + 1 == _nextId)
				{
					--_nextId;
				}
				else
				{
					_freeIDs.push_back(e.id);
				}
			}

			_deferred.clear();
		}

	private:
		cstdmf::SparseSet<Entity>	_entities;
		std::vector<Entity::ID>		_freeIDs;
		Entity::ID              _nextId = 0;
		ComponentsMeta				_meta;

		DeferredCommands _deferred;

		events::PublishToken _entityCreateToken;
		events::PublishToken _entityDestroyToken;

		events::EventBus2& _bus;
		//
	};

}

#endif;
