#ifndef __CSYREN_SCENE__
#define __CSYREN_SCENE__

#include <limits>
#include <vector>
#include <unordered_map>
#include <memory>

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
		T* addComponent(Entity::ID id, Args&&... args)
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
		bool hasComponent(Entity::ID id)
		{
			Entity* ent = _entities.try_get(id);
			if (!ent) return false;

			const size_t family = reflection::ComponentFamily::getID<T>();
			return ent->components.test(family);

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
		SceneView<Cs...> view(){ return SceneView<Cs...>(this); }

		const cstdmf::SparseSet<Entity>& entities() const { return _entities; }


		void flush()
		{
			for (const auto& e : _deferred.destroyComponentBuf())
			{
				auto it = _meta.find(e.family);
				if (it == _meta.end())
					continue;
				it->second.removeFn(this, e, it->second.removeToken, _bus);
			}

			DestroyComponentCommand cm;
			for (const auto& e : _deferred.destroyEntityBuf())
			{

				Entity* ent = _entities.try_get(e.id);
				if (!ent) continue;
				for (const auto& [family, m] : _meta)
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


	template<class... Cs>
	class SceneView
	{
		template<class T>
		using PoolPtr = std::shared_ptr<ComponentPool<T>>;
		using Pools = std::tuple<PoolPtr<Cs>...>;
		using DenseContainer = std::vector<Entity::ID>;
		using DenseIt = DenseContainer::const_iterator;

		template <typename F, typename Tuple, typename = void>
		struct is_apply_invocable : std::false_type {};

		template <typename F, typename Tuple>
		struct is_apply_invocable<F, Tuple,
			std::void_t<decltype(std::apply(std::declval<F>(), std::declval<Tuple>()))>
		> : std::true_type {
		};
	public:
		SceneView(Scene* scene) : _scene(scene),_empty(true){}
		class iterator
		{
		public:
			using value_type = std::tuple<Entity::ID, Cs&...>;
			using iterator_category = std::forward_iterator_tag;
			using difference_type = std::ptrdiff_t;

			iterator(SceneView* view, DenseIt it)
				: _view(view), _it(it) 
			{
				skip();
			}
			value_type operator*() const
			{
				Entity::ID ent = *_it;
				return const_cast<SceneView*>(_view) ->make_pointer_tuple(ent);
			}
			iterator& operator++() { ++_it; skip(); return *this; }
			iterator  operator++(int) { iterator tmp{ *this }; ++(*this); return tmp; }

			friend bool operator==(const iterator& a, const iterator& b) { return a._it == b._it; }
			friend bool operator!=(const iterator& a, const iterator& b) { return !(a == b); }
		private:

			void skip()
			{
				while (_it != _view->_last && !_view->has_all_components(*_it))
					++_it;
			}
			SceneView* _view;
			DenseIt    _it;
		};

		class const_iterator
		{
		public:
			using value_type = std::tuple<Entity::ID, const Cs&...>;
			using iterator_category = std::forward_iterator_tag;

			const_iterator(const SceneView* view, DenseIt it) : _view(view), _it(it) { skip(); }

			value_type operator*() const
			{
				Entity::ID ent = *_it;
				return _view->make_pointer_tuple(ent);
			}

			const_iterator& operator++() { ++_it; skip(); return *this; }
			const_iterator  operator++(int) { const_iterator tmp{ *this }; ++(*this); return tmp; }

			friend bool operator==(const const_iterator& a, const const_iterator& b)
			{
				return a._it == b._it;
			}
			friend bool operator!=(const const_iterator& a, const const_iterator& b)
			{
				return !(a == b);
			}

		private:
			void skip()
			{
				while (_it != _view->_last && !_view->has_all_components(*_it))
					++_it;
			}
			const SceneView* _view;
			DenseIt    _it;
		};

		[[nodiscard]] iterator       begin() { refresh(); return _empty ? end() : iterator(this, _first); }
		[[nodiscard]] iterator       end() { return iterator(this, _last); }

		[[nodiscard]] const_iterator begin() const { refresh(); return _empty ? end() : const_iterator(this, _first); }
		[[nodiscard]] const_iterator end()   const { return const_iterator(this, _last); }

		template<class Fn>
		void each(Fn&& fn)
		{
			using element_type = std::decay_t<decltype(*std::declval<decltype(begin())>())>;
			static_assert(is_apply_invocable<Fn&&, element_type>::value,
				"function object must be callable via SceneView");
			for (auto it = begin(); it != end(); ++it)
				std::apply(fn, *it);
		}

	private:
		auto make_pointer_tuple(Entity::ID id) const
		{
			return std::tuple<Entity::ID,const Cs&...>(id, (*std::get<PoolPtr<Cs>>(_pools))[id]...);
		}
		auto make_pointer_tuple(Entity::ID id)
		{
			return std::tuple<Entity::ID, Cs&...>(id, (*std::get<PoolPtr<Cs>>(_pools))[id]...);
		}
		
		void gather_pools() const
		{
			((std::get<PoolPtr<Cs>>(_pools) = _scene->template getPool<Cs>()), ...);
		}

		void refresh() const
		{
			if (!_empty)
			{
				pick_smallest();
				return;
			}

			gather_pools();

			if ((!std::get<PoolPtr<Cs>>(_pools) || ...))
			{
				return;
			}
			_empty = false;
			pick_smallest();
		}

		void pick_smallest() const
		{
			DenseIt first{}, last{};
			std::size_t minSize = std::numeric_limits<std::size_t>::max();
			std::apply([&](auto&&... pool)
				{
					((pool->size() < minSize ? (minSize = pool->size(),first = pool->key_begin(),last = pool->key_end(),0): 0), ...);
				}, _pools);

			_first = first;
			_last = last;
		}
		bool has_all_components(Entity::ID id) const
		{
			return (std::get<PoolPtr<Cs>>(_pools)->contains(id) && ...);
		}

		mutable Pools _pools;
		Scene* _scene;
		mutable DenseIt _first, _last;
		mutable bool _empty{ false };
	};

}

#endif;
