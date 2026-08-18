#ifndef __CSYREN_SCENE__
#define __CSYREN_SCENE__

#include <limits>
#include <vector>
#include <unordered_map>
#include <memory>

#include "cstdmf/assert_helpler.h"
#include "component_base.h"
#include "component_pool.h"
#include "component_order.h"

#include "entity_manager.h"

#include "command_buffer.h"
#include "event_bus.h"
#include "services.h"

class SceneTest;

namespace csyren::core::details
{

}

namespace csyren::core
{
	template<typename T>
	class ComponentRef
	{
	public:
		ComponentRef() = default;
		ComponentRef(Entity::ID id, std::shared_ptr<ComponentPool<T>> pool) :
			_pool(pool),
			_id(id)
		{}

		T* get() noexcept 
		{
			_cached = _pool ? _pool->try_get(_id) : nullptr;
			return _cached;
		}

		const T* get() const noexcept 
		{
			_cached = _pool ? _pool->try_get(_id) : nullptr;
			return _cached;
		}

		T& operator*() noexcept
		{
			T* ptr = get();
			CS_ASSERT(ptr && "Dereferencing invalid ComponentRef!");
			return *ptr;
		}

		const T& operator*() const noexcept
		{
			const T* ptr = get();
			CS_ASSERT(ptr && "Dereferencing invalid ComponentRef!");
			return *ptr;
		}

		T* operator->() noexcept { return get(); }
		const T* operator->() const noexcept { return get(); }
		Entity::ID id() const noexcept { return _id; }

		bool operator==(std::nullptr_t) const { return !static_cast<bool>(*this); }
		bool operator!=(std::nullptr_t) const { return static_cast<bool>(*this); }

		bool operator==(const ComponentRef& other) const noexcept 
		{
			return _id == other._id && _pool == other._pool;
		}

		bool operator!=(const ComponentRef& other) const noexcept
		{
			return !(*this == other);
		}
		operator bool() const noexcept { return _pool && _pool->contains(_id); }
	private:

		std::shared_ptr<ComponentPool<T>> _pool{ nullptr };
		Entity::ID _id{ Entity::invalidID };
		mutable T* _cached{ nullptr };//for debug olny;it is not save to address to this field.
	};

}

namespace csyren::core
{
	struct EntityCreateEvent { Entity::ID id; };
	struct EntityDestroyEvent { Entity::ID id; };

	template<typename T>
	struct ComponentCreateEvent
	{
		ComponentRef<T> comp;
	};

	template<typename T>
	struct ComponentDestroyEvent
	{
		ComponentRef<T> comp;
	};
}

namespace csyren::core
{
	template<typename... Cs>
	class SceneView;

	//class Application;
	class Scene
	{
		friend class Application;
		friend SceneTest;
		template<typename...> friend class SceneView;

		using DestructFn = bool(Scene*, Entity::ID);
		using GetRawFn = void* (Scene*, Entity::ID);
		struct ComponentMeta
		{
			std::shared_ptr<PoolBase> pool;
			PublishToken      addToken;
			PublishToken      removeToken;
			GetRawFn* getRawFn = nullptr;
			DestructFn* removeFn = nullptr;

		};
		using ComponentsMeta = std::unordered_map<size_t, ComponentMeta>;

		template<typename T>
		struct ComponentOps
		{
			static void* getRawThunk(Scene* self, Entity::ID id)
			{
				auto pool = self->getPool<T>();
				return pool ? pool->try_get(id) : nullptr;
			}

			static bool destroyThunk(Scene* self,const Entity::ID id)
			{
				auto pool = self->getPool<T>();
				T* ptr = pool ? pool->try_get(id) : nullptr;
				if (ptr)
				{
					auto family = reflection::ComponentFamily::getID<T>();
					const auto& meta = self->_meta[family];
					self->_bus->publish(meta.removeToken, ComponentDestroyEvent<T>{ComponentRef<T>(id,pool)});
					pool->erase(id);
					if (Entity* ent = self->_em.tryGet(id))
					{
						ent->remove(family);
					}
					return true;
				}
				return false;
			}
		};
		static constexpr std::string_view DEFAULT_NAME{ "new Entity" };
		static constexpr Entity::ID ROOT_PARENT = 0;

	public:
		explicit Scene(EventBus2* bus) : _bus(bus) {}

		void init()
		{
			if (!_bus)
			{
				log::error("Scene::init: EventBus not provided");
				return;
			}
			_entityCreateToken = _bus->register_publisher<EntityCreateEvent>();
			_entityDestroyToken = _bus->register_publisher<EntityDestroyEvent>();
			_em.init();			
		}

		bool setName(Entity::ID id, std::string_view newName) { return _em.setName(id, newName); }

		[[nodiscard]] Entity::ID createEntity(Entity::ID parent = EntityManager::ROOT_PARENT)
		{
			return createEntity(EntityManager::DEFAULT_NAME, parent);
		}

		[[nodiscard]] Entity::ID createEntity(std::string_view name,
			Entity::ID parent = EntityManager::ROOT_PARENT)
		{
			Entity::ID id = _em.createEntity(name, parent);
			_bus->publish(_entityCreateToken, EntityCreateEvent{ id });
			return id;
		}

		void destroyEntity(Entity::ID id) { _em.queueDestroy(id); }

		template<typename T, typename... Args>
		ComponentRef<T> addComponent(Entity::ID id, Args&&... args)
		{
			Entity* ent = _em.tryGet(id);
			if (!ent) return ComponentRef<T>();

			const size_t family = reflection::ComponentFamily::getID<T>();
			if (ent->has(family)) throw std::runtime_error("Component Already presented)");

			auto pool = getOrCreatePool<T>(family);
			T* ptr = pool->emplace(id, std::forward<Args>(args)...);
			ComponentRef<T> compRef(id, pool);
			if (ptr)
			{
				ent->add(family);
				_bus->publish(getAddToken<T>(),
					ComponentCreateEvent<T>{compRef});
			}
			return compRef;
		}

		template<typename T> bool          hasComponent(Entity::ID id) { return _em.contains(id) && _em.tryGet(id)->has(reflection::ComponentFamily::getID<T>()); }
		template<typename T> void          removeComponent(Entity::ID id) { removeComponent(id, reflection::ComponentFamily::getID<T>()); }
		void                               removeComponent(Entity::ID id, size_t family)
		{
			if (Entity* ent = _em.tryGet(id); ent && ent->has(family))
				_deferred.emplace_back(DestroyComponentCommand{ id, family });
		}

		template<typename T> ComponentRef<T> getComponent(Entity::ID id)
		{
			return _em.contains(id) ? ComponentRef<T>(id, getPool<T>()) : ComponentRef<T>();
		}

		void* getComponentRaw(Entity::ID id, size_t family)
		{
			auto it = _meta.find(family);
			if (it == _meta.end() || !it->second.getRawFn) return nullptr;
			return it->second.getRawFn(this, id);
		}

		template<typename... Cs>
		SceneView<Cs...> view(){ return SceneView<Cs...>(this); }

		const cstdmf::SparseSet<Entity>& entities() const { return _em.all(); }


		void flush()
		{
			for (const auto& e : _deferred)
			{
				auto it = _meta.find(e.family);
				if (it == _meta.end())
					continue;
				it->second.removeFn(this, e.entt);
			}

			auto destroyList = _em.collectDestroyList();

			DestroyComponentCommand cm;
			for (Entity::ID id : destroyList)
			{
				Entity* ent = _em.tryGet(id);
				if (!ent) continue;
				for (const auto& family : ent->componentView())
				{
					auto it = _meta.find(family);
					if (it == _meta.end()) continue;

					it->second.removeFn(this, id);
				}

				_bus->publish(_entityDestroyToken, EntityDestroyEvent{ id });
				_em.finalizeDestroy(id);
			}

			_em.clearPending();
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
			m.addToken = _bus->register_publisher<ComponentCreateEvent<T>>();
			m.removeToken = _bus->register_publisher<ComponentDestroyEvent<T>>();
			m.removeFn = &ComponentOps<T>::destroyThunk;
			m.getRawFn = &ComponentOps<T>::getRawThunk;
		}
		template<typename T>
		PublishToken& getAddToken()
		{
			size_t family = reflection::ComponentFamily::getID<T>();
			if (!_meta.contains(family)) getOrCreatePool<T>(family);
			return _meta[family].addToken;
		}

		template<typename T>
		PublishToken& getRemoveToken()
		{
			size_t family = reflection::ComponentFamily::getID<T>();
			if (!_meta.contains(family)) getOrCreatePool<T>(family);
			return _meta[family].removeToken;
		}


	private:
		EntityManager				_em;
		ComponentsMeta				_meta;

		std::vector<DestroyComponentCommand> _deferred;

		PublishToken _entityCreateToken;
		PublishToken _entityDestroyToken;

		EventBus2* _bus;
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

		[[nodiscard]] bool contains(Entity::ID id) const
		{
			refresh();
			if (_empty)
			{
				return false;
			}
			return has_all_components(id);
		}

		[[nodiscard]] std::tuple<Cs&...> get(Entity::ID id)
		{
			assert(contains(id) && "Entity does not belong to this view");
			return std::tie((*std::get<PoolPtr<Cs>>(_pools))[id]...);
		}
		[[nodiscard]] std::tuple<const Cs&...> get(Entity::ID id) const
		{
			assert(contains(id) && "Entity does not belong to this view");
			return std::tie((*std::get<PoolPtr<Cs>>(_pools))[id]...);
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
