#ifndef __CSYREN_SCENE__
#define __CSYREN_SCENE__

#include <limits>
#include <vector>
#include <unordered_map>
#include <memory>
#include <stdexcept>
#include <functional>
#include <type_traits>

#include "cstdmf/assert_helpler.h"
#include "component_base.h"
#include "component_pool.h"
#include "component_order.h"

#include "entity_manager.h"

#include "component_registry.h"
#include "entity_batch.h"
#include "command_buffer.h"
#include "event_bus.h"
#include "services.h"
#include "task_description.h"

class SceneTest;

namespace csyren::core::details
{
    struct SceneJobTestAccess;
}

namespace csyren::core
{
    template<class T> struct StagedComponent {
        std::shared_ptr<T> value;
        std::shared_ptr<ComponentPool<T>> pool;
        uint64_t version = 0;
    };
    template<typename T> class ComponentRef {
        using Value = std::remove_const_t<T>;
    public:
        template<class> friend class ComponentRef;
        friend class Scene;
        ComponentRef() = default;
        template<class U> requires (std::is_const_v<T> && std::is_same_v<U, Value>)
        ComponentRef(const ComponentRef<U>& other) : _pool(other._pool), _id(other._id), _version(other._version), _staged(other._staged) {}
        ComponentRef(Entity::ID id, std::shared_ptr<ComponentPool<Value>> pool)
            : _pool(std::move(pool)), _id(id), _version(_pool ? _pool->version(id) : 0) {}
        ComponentRef(Entity::ID id, std::shared_ptr<StagedComponent<Value>> staged)
            : _id(id), _staged(std::move(staged)) {}
        T* get() noexcept {
            if (_staged) {
                if (_staged->value) return _staged->value.get();
                return _staged->pool && _staged->pool->version(_id) == _staged->version
                    ? _staged->pool->try_get(_id) : nullptr;
            }
            return _pool && _pool->version(_id) == _version ? _pool->try_get(_id) : nullptr;
        }
        const T* get() const noexcept { return const_cast<ComponentRef*>(this)->get(); }
        T& operator*() noexcept { auto* p = get(); CS_ASSERT(p); return *p; }
        const T& operator*() const noexcept { auto* p = get(); CS_ASSERT(p); return *p; }
        T* operator->() noexcept { return get(); }
        const T* operator->() const noexcept { return get(); }
        Entity::ID id() const noexcept { return _id; }
        explicit operator bool() const noexcept { return get() != nullptr; }
        bool operator==(std::nullptr_t) const { return !static_cast<bool>(*this); }
        bool operator!=(std::nullptr_t) const { return static_cast<bool>(*this); }
        bool operator==(const ComponentRef& other) const noexcept {
            if (_id != other._id) return false;
            if ((_staged && _staged->value) || (other._staged && other._staged->value)) return _staged == other._staged;
            return (_staged ? _staged->pool : _pool) == (other._staged ? other._staged->pool : other._pool) &&
                (_staged ? _staged->version : _version) == (other._staged ? other._staged->version : other._version);
        }
        bool operator!=(const ComponentRef& other) const noexcept { return !(*this == other); }
    private:
        std::shared_ptr<ComponentPool<Value>> _pool;
        Entity::ID _id = Entity::invalidID;
        uint64_t _version = 0;
        std::shared_ptr<StagedComponent<Value>> _staged;
    };

}

namespace csyren::core
{
	struct EntityCreateEvent { Entity::ID id; };
	struct EntityDestroyEvent { Entity::ID id; };
    struct EntityReparentEvent { Entity::ID id, previous, parent; };
    template<class T> struct ComponentPatchEvent { ComponentRef<T> comp; };

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
        friend class SystemManager;
        friend struct details::SceneJobTestAccess;
		friend class Application;
		friend reflection::ComponentRegistry;
		friend SceneTest;
        friend class EntityBatch;
		template<typename...> friend class SceneView;

		using DestructFn = bool(Scene*, Entity::ID);
		using GetRawFn = void* (Scene*, Entity::ID);
		struct ComponentState
		{
			std::shared_ptr<PoolBase> pool;
			PublishToken      addToken;
			PublishToken      removeToken;
            PublishToken patchToken;
            uint64_t revision = 0;
			const reflection::ComponentMeta* meta;

		};
		using ComponentStates = std::unordered_map<size_t, ComponentState>;


	public:

        EntityBatch stageEntities(size_t count) const { return EntityBatch(_em.planAppend(count)); }
        // The batch's first entity gets `root`, an ID from reserveEntityId().
        EntityBatch stageEntities(size_t count, Entity::ID root) const {
            if (root == Entity::invalidID || count == 0) return stageEntities(count);
            auto planned = _em.planAppend(count - 1);
            planned.insert(planned.begin(), root);
            return EntityBatch(std::move(planned), 1);
        }
        // An ID for an entity created later (as a batch root, e.g. by Spawn), known
        // now. Thread safe. releaseEntityId() gives an unused one back: it never
        // becomes an entity.
        [[nodiscard]] Entity::ID reserveEntityId() { requireStructuralAccess(); return _em.reserveId(); }
        void releaseEntityId(Entity::ID id) { _em.releaseReserved(id); }

        // The batch is consumed only on success. No scene data changes before
        // preparation completes; the commit performs only prepared, no-throw moves.
        // Scene mutation and EventBus dispatch must not run concurrently with append.
        bool append(EntityBatch&& data);

    private:
        // Owned by a graph task. Public Scene methods automatically use the active
        // transaction; users never manage command buffers or temporary entity IDs.
        struct ComponentKey {
            Entity::ID id; size_t family;
            bool operator==(const ComponentKey&) const = default;
        };
        struct ComponentKeyHash {
            size_t operator()(const ComponentKey& key) const noexcept {
                return std::hash<uint64_t>{}(key.id * 0x9E3779B97F4A7C15ull ^ uint64_t(key.family));
            }
        };
        class Transaction {
            friend class Scene;
            template<typename...> friend class SceneView;
            std::vector<std::function<void()>> commands;
            std::vector<std::function<void()>> cancellations;
            // Staged additions (StagedComponent<T>) and removals (nullptr) by
            // componentKey(id, family); the key already names the type.
            std::unordered_map<ComponentKey, std::shared_ptr<void>, ComponentKeyHash> components;
            std::unordered_map<Entity::ID, bool> entities;
        public:
            void apply(Scene& scene) {
                try {
                    for (auto& command : commands) command();
                    scene.flush();
                } catch (...) { discard(); throw; }
                discard();
            }
            void discard() {
                for (auto& cancel : cancellations) cancel();
                cancellations.clear(); commands.clear(); components.clear(); entities.clear();
            }
        };
        class TaskScope {
        public:
            TaskScope(Scene& scene, Transaction* transaction, const TaskDescription* description = nullptr) : _previous(_active), _binding{&scene, transaction, description} { _active = &_binding; }
            ~TaskScope() { _active = _previous; }
            TaskScope(const TaskScope&) = delete;
        private:
            struct Binding { Scene* scene; Transaction* transaction; const TaskDescription* description; };
            Binding* _previous;
            Binding _binding;
            inline static thread_local Binding* _active = nullptr;
            friend class Scene;
        };

    public:
		EventBus2& eventBus() const { return *_bus; }
		explicit Scene(EventBus2* bus) : _bus(bus) {}
        uint64_t identity() const { return _identity; }
        uint64_t hierarchyRevision() const { (void)entities(); return _hierarchyRevision; }
        template<class T> uint64_t componentRevision() const {
            requireComponentAccess<const T>();
            auto it = _cStates.find(reflection::ComponentFamily::getID<std::remove_const_t<T>>());
            return it == _cStates.end() ? 0 : it->second.revision;
        }

		void init()
		{
			if (!_bus)
			{
				log::error("Scene::init: EventBus not provided");
				return;
			}
			_entityCreateToken = _bus->register_publisher<EntityCreateEvent>();
			_entityDestroyToken = _bus->register_publisher<EntityDestroyEvent>();
            _entityReparentToken = _bus->register_publisher<EntityReparentEvent>();
			_em.init();			
		}

		bool setParent(Entity::ID id, Entity::ID parent) {
            requireStructuralAccess();
            if (auto* tx = transaction()) { tx->commands.emplace_back([this, id, parent] { setParent(id, parent); }); return true; }
            const auto* entity = _em.tryGet(id);
            if (!entity) return false;
            const auto previous = entity->parent;
            if (!_em.setParent(id, parent)) return false;
            if (previous != parent) {
                ++_hierarchyRevision;
                _bus->publish(_entityReparentToken, EntityReparentEvent{id, previous, parent});
            }
            return true;
        }
        bool setName(Entity::ID id, std::string_view newName) {
            requireStructuralAccess();
            if (auto* tx = transaction()) {
                std::string name(newName);
                tx->commands.emplace_back([this, id, name = std::move(name)] { _em.setName(id, name); });
                return tx->entities.contains(id) || _em.contains(id);
            }
            return _em.setName(id, newName);
        }

		[[nodiscard]] Entity::ID createEntity(Entity::ID parent = EntityManager::ROOT_PARENT)
		{
			return createEntity(EntityManager::DEFAULT_NAME, parent);
		}

		[[nodiscard]] Entity::ID createEntity(std::string_view name,
			Entity::ID parent = EntityManager::ROOT_PARENT)
		{
            requireStructuralAccess();
            if (auto* tx = transaction()) {
                const auto id = _em.reserveId();
                tx->entities.emplace(id, true);
                tx->commands.emplace_back([this, id, parent, name = std::string(name)] {
                    _em.createReservedEntity(id, name, parent);
                    ++_hierarchyRevision;
                    _bus->publish(_entityCreateToken, EntityCreateEvent{id});
                });
                return id;
            }
            Entity::ID id = _em.createEntity(name, parent);
            ++_hierarchyRevision;
			_bus->publish(_entityCreateToken, EntityCreateEvent{ id });
			return id;
		}

		void destroyEntity(Entity::ID id) {
            requireStructuralAccess();
            if (auto* tx = transaction()) {
                tx->commands.emplace_back([this, id] { _em.queueDestroy(id); });
                return;
            }
            _em.queueDestroy(id);
        }

        // A structural change whose types are known only at run time (core::Spawn):
        // runs in the active task's structural commit, in order with its other
        // changes; immediately outside a graph task.
        void defer(std::function<void()> command) {
            requireStructuralAccess();
            if (auto* tx = transaction()) { tx->commands.emplace_back(std::move(command)); return; }
            command();
        }

		template<typename T, typename... Args>
		ComponentRef<T> addComponent(Entity::ID id, Args&&... args)
        {
            requireStructuralAccess();
            requireInsertAccess<T>();
            if (auto* tx = transaction()) {
                if (!tx->entities.contains(id) && !_em.contains(id)) return {};
                const auto family = reflection::ComponentFamily::getID<T>();
                const auto key = componentKey(id, family);
                // Presence only: inserting T does not require read access to T.
                if (lookupComponent<const T>(id)) throw std::runtime_error("Component already present");
                auto staged = std::make_shared<StagedComponent<T>>();
                staged->value = std::make_shared<T>(std::forward<Args>(args)...);
                ComponentRef<T> result(id, staged);
                tx->components.insert_or_assign(key, staged);
                tx->cancellations.emplace_back([staged] { if (staged->value) staged->value.reset(); });
                tx->commands.emplace_back([this, id, staged] {
                    auto live = addComponent<T>(id, std::move_if_noexcept(*staged->value));
                    staged->pool = getPool<T>();
                    staged->version = staged->pool ? staged->pool->version(id) : 0;
                    staged->value.reset();
                });
                return result;
            }
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
                ++_cStates.at(family).revision;
                _bus->publish(getAddToken<T>(),
					ComponentCreateEvent<T>{compRef});
			}
			return compRef;
		}

		template<typename T> bool hasComponent(Entity::ID id) { return bool(getComponent<const std::remove_const_t<T>>(id)); }
		template<typename T> void removeComponent(Entity::ID id) { requireComponentAccess<T>(); removeComponent(id, reflection::ComponentFamily::getID<T>()); }
		void                               removeComponent(Entity::ID id, size_t family)
		{
            requireStructuralAccess();
            if (auto* tx = transaction()) {
                tx->components.insert_or_assign(componentKey(id, family), nullptr);
                tx->commands.emplace_back([this, id, family] {
                    auto it = _cStates.find(family);
                    if (it != _cStates.end()) it->second.meta->destroy(this, id);
                });
                return;
            }
            if (Entity* ent = _em.tryGet(id); ent && ent->has(family))
                _deferred.emplace_back(DestroyComponentCommand{ id, family });
		}

		template<typename T> ComponentRef<T> getComponent(Entity::ID id)
		{
            requireComponentAccess<T>();
            return lookupComponent<T>(id);
		}

    private:
        template<typename T> ComponentRef<T> lookupComponent(Entity::ID id)
        {
            using Value = std::remove_const_t<T>;
            if (auto* tx = transaction()) {
                auto it = tx->components.find(componentKey(id, reflection::ComponentFamily::getID<Value>()));
                if (it != tx->components.end()) {
                    if (!it->second) return {}; // removed in this transaction
                    return ComponentRef<T>(id, std::static_pointer_cast<StagedComponent<Value>>(it->second));
                }
            }
            return ComponentRef<T>(id, getPool<Value>());
        }

    public:
        // A patch notifies derived data after a mutation. The callback overload
        // also reports partial writes if the callback throws. It does not add an
        // undo command; editor history is a separate concern.
        template<class T> bool patch(ComponentRef<T> component) {
            static_assert(!std::is_const_v<T>);
            requireComponentAccess<T>();
            if (!component || component != getComponent<T>(component.id())) return false;
            if (auto* tx = transaction(); tx && component._staged && component._staged->value) {
                tx->commands.emplace_back([this, component] { patch(component); });
                return true;
            }
            auto& state = _cStates.at(reflection::ComponentFamily::getID<T>());
            ++state.revision;
            _bus->publish(state.patchToken, ComponentPatchEvent<T>{component});
            return true;
        }
        template<class T, class Edit> bool patch(ComponentRef<T> component, Edit&& edit) {
            requireComponentAccess<T>();
            if (!component || component != getComponent<T>(component.id())) return false;
            try { std::invoke(std::forward<Edit>(edit), *component); }
            catch (...) { patch(component); throw; }
            return patch(component);
        }
        template<class T> bool patch(Entity::ID id) { return patch(getComponent<T>(id)); }
        template<class T, class Edit> bool patch(Entity::ID id, Edit&& edit) {
            return patch(getComponent<T>(id), std::forward<Edit>(edit));
        }
        bool patch(Entity::ID id, size_t family) {
            auto* meta = reflection::ComponentRegistry::get(family);
            return meta && meta->patch && meta->patch(this, id);
        }

		void* getComponentRaw(Entity::ID id, size_t family)
		{
			auto it = _cStates.find(family);
			if (it == _cStates.end()) return nullptr;
			return it->second.meta->getRaw(this, id);
		}

		template<typename... Cs>
		SceneView<Cs...> view(){ (requireComponentAccess<Cs>(), ...); return SceneView<Cs...>(this); }

        // Validation hooks for typed queries (core/typed_system.h), which build their
        // views directly: Cs are checked like view<Cs...>(), filter types only for
        // presence. No-ops without CSYREN_ACCESS_VALIDATION.
        template<typename T> void validateAccess() const { requireComponentAccess<T>(); }
        template<typename T> void validatePresence() const { requirePresenceAccess<T>(); }

		const EntitySet<Entity>& entities() const {
#if CSYREN_ACCESS_VALIDATION
            const auto* b = TaskScope::_active;
            if (b && b->scene == this && b->description && !b->description->exclusiveScene && !b->description->structuralChanges) {
                const auto& accesses = b->description->accesses;
                if (std::none_of(accesses.begin(), accesses.end(), [](const auto& a) {
                    return !a.component && a.type == TypeKey::of<SceneEntities>();
                })) throw std::logic_error("System must declare SceneEntities access before enumerating entities");
            }
#endif
            return _em.all();
        }


        void flush()
        {
            if (transaction()) return; // The graph owns the safe application point.
			for (const auto& e : _deferred)
			{
				auto it = _cStates.find(e.family);
				if (it == _cStates.end())
					continue;
				it->second.meta->destroy(this, e.entt);
			}

			auto destroyList = _em.collectDestroyList();

			for (Entity::ID id : destroyList)
			{
				Entity* ent = _em.tryGet(id);
				if (!ent) continue;
                // destroy removes the family from componentView; iterate a copy.
                const auto families = ent->componentView();
				for (const auto family : families)
				{
					auto it = _cStates.find(family);
					if (it == _cStates.end()) continue;

					it->second.meta->destroy(this, id);
				}

				_bus->publish(_entityDestroyToken, EntityDestroyEvent{ id });
				_em.finalizeDestroy(id);
                ++_hierarchyRevision;
			}

			_em.clearPending();
			_deferred.clear();
		}

	private:

        void requireStructuralAccess() const {
            const auto* b = TaskScope::_active;
            if (b && b->scene == this && !b->transaction)
                throw std::logic_error("System must declare structuralChanges before changing Scene structure");
        }
        template<class T> void requireComponentAccess() const {
#if CSYREN_ACCESS_VALIDATION
            const auto* b = TaskScope::_active;
            if (!b || b->scene != this || !b->description || b->description->exclusiveScene) return;
            const auto type = TypeKey::of<T>();
            for (const auto& a : b->description->accesses)
                if (a.component && a.type == type && (std::is_const_v<T> || a.mode == AccessMode::Write)) return;
            throw std::logic_error("Undeclared component access in system: " + std::string(type.name));
#endif
        }
        // Presence tests only: a filter, read or write declaration of T.
        template<class T> void requirePresenceAccess() const {
#if CSYREN_ACCESS_VALIDATION
            const auto* b = TaskScope::_active;
            if (!b || b->scene != this || !b->description || b->description->exclusiveScene) return;
            const auto type = TypeKey::of<T>();
            const auto& filters = b->description->filters;
            if (std::find(filters.begin(), filters.end(), type) != filters.end()) return;
            for (const auto& a : b->description->accesses)
                if (a.component && a.type == type) return;
            throw std::logic_error("Undeclared component filter in system: " + std::string(type.name));
#endif
        }
        // addComponent: write access, or an insert declaration for the staged type.
        template<class T> void requireInsertAccess() const {
#if CSYREN_ACCESS_VALIDATION
            const auto* b = TaskScope::_active;
            if (!b || b->scene != this || !b->description || b->description->exclusiveScene) return;
            const auto type = TypeKey::of<T>();
            const auto& inserts = b->description->inserts;
            if (std::find(inserts.begin(), inserts.end(), type) != inserts.end()) return;
            for (const auto& a : b->description->accesses)
                if (a.component && a.type == type && a.mode == AccessMode::Write) return;
            throw std::logic_error("Undeclared component insert in system: " + std::string(type.name));
#endif
        }
        Transaction* transaction() const {
            auto* binding = TaskScope::_active;
            return binding && binding->scene == this ? binding->transaction : nullptr;
        }
        static ComponentKey componentKey(Entity::ID id, size_t family) { return { id, family }; }

		template<class T>
		bool destroy(Entity::ID id)
		{
			auto pool = getPool<T>();
			T* ptr = pool ? pool->try_get(id) : nullptr;
			if (ptr)
			{
				auto family = reflection::ComponentFamily::getID<T>();
                _bus->publish(_cStates[family].removeToken, ComponentDestroyEvent<T>{ComponentRef<T>(id, pool)});
				pool->erase(id);
                ++_cStates.at(family).revision;
				if (Entity* ent = _em.tryGet(id))
				{
					ent->remove(family);
				}
				return true;
			}
			return false;
		}

		template<typename T>
		std::shared_ptr<ComponentPool<T>> getPool()
		{
			size_t family = reflection::ComponentFamily::getID<T>();
			auto it = _cStates.find(family);
			if (it != _cStates.end())
				return std::static_pointer_cast<ComponentPool<T>>(it->second.pool);
			return nullptr;
		}
		template<typename T>
		std::shared_ptr<ComponentPool<T>> getOrCreatePool(size_t family)
		{
			if (auto pool = getPool<T>()) return pool;

			auto newPool = std::make_shared<ComponentPool<T>>();
			_cStates[family].pool = newPool;
			CS_ASSERT(registerOps<T>() == true);
			return newPool;
		}

		template<typename T>
		bool registerOps()
		{
			const size_t family = reflection::ComponentFamily::getID<T>();
			auto meta = reflection::ComponentRegistry::get(family);
			if (!meta)
			{
				log::error("Attempt to add unregistered component to the scene. register it first\n");
				return false;
			}
			auto& m = _cStates[family];
			m.addToken = _bus->register_publisher<ComponentCreateEvent<T>>();
			m.removeToken = _bus->register_publisher<ComponentDestroyEvent<T>>();
            m.patchToken = _bus->register_publisher<ComponentPatchEvent<T>>();
			m.meta = meta;
			return true;
		}
		template<typename T>
		PublishToken& getAddToken()
		{
			size_t family = reflection::ComponentFamily::getID<T>();
			if (!_cStates.contains(family)) getOrCreatePool<T>(family);
			return _cStates[family].addToken;
		}

		template<typename T>
		PublishToken& getRemoveToken()
		{
			size_t family = reflection::ComponentFamily::getID<T>();
			if (!_cStates.contains(family)) getOrCreatePool<T>(family);
			return _cStates[family].removeToken;
		}


	private:
		EntityManager				_em;
		ComponentStates				_cStates;

		std::vector<DestroyComponentCommand> _deferred;

		PublishToken _entityCreateToken;
		PublishToken _entityDestroyToken;
        PublishToken _entityReparentToken;
        inline static std::atomic<uint64_t> _nextIdentity{0};
        const uint64_t _identity = ++_nextIdentity;
        uint64_t _hierarchyRevision = 0;

		EventBus2* _bus;
		//
	};


	template<class... Cs>
	class SceneView
	{
		template<class T>
		using PoolPtr = std::shared_ptr<ComponentPool<std::remove_const_t<T>>>;
		using Pools = std::tuple<PoolPtr<Cs>...>;
		using DenseContainer = std::vector<Entity::ID>;
		using DenseIt = DenseContainer::const_iterator;

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
		void each(Fn&& fn) requires std::invocable<Fn&&, Entity::ID, Cs&...>
		{ 
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
            if (_overlayMode) return std::tuple<Cs&...>(*(_scene->template lookupComponent<Cs>(id))...);
			return std::tie((*std::get<PoolPtr<Cs>>(_pools))[id]...);
		}
		[[nodiscard]] std::tuple<const Cs&...> get(Entity::ID id) const
		{
			assert(contains(id) && "Entity does not belong to this view");
            if (_overlayMode) return std::tuple<const Cs&...>(*(_scene->template lookupComponent<const std::remove_const_t<Cs>>(id))...);
			return std::tie((*std::get<PoolPtr<Cs>>(_pools))[id]...);
		}

	private:
		auto make_pointer_tuple(Entity::ID id) const
		{
            if (_overlayMode) return std::tuple<Entity::ID, const Cs&...>(id, *(_scene->template lookupComponent<const std::remove_const_t<Cs>>(id))...);
			return std::tuple<Entity::ID,const Cs&...>(id, (*std::get<PoolPtr<Cs>>(_pools))[id]...);
		}
		auto make_pointer_tuple(Entity::ID id)
		{
            if (_overlayMode) return std::tuple<Entity::ID, Cs&...>(id, *(_scene->template lookupComponent<Cs>(id))...);
			return std::tuple<Entity::ID, Cs&...>(id, (*std::get<PoolPtr<Cs>>(_pools))[id]...);
		}
		
		void gather_pools() const
		{
			((std::get<PoolPtr<Cs>>(_pools) = _scene->template getPool<std::remove_const_t<Cs>>()), ...);
		}

        void refresh() const
        {
            if (auto* tx = _scene->transaction(); tx && !tx->components.empty()) {
                if (!_overlayMode) {
                    gather_pools();
                    size_t smallest = std::numeric_limits<size_t>::max();
                    std::apply([&](auto&... pool) {
                        ([&] {
                            if (pool && pool->size() < smallest) {
                                smallest = pool->size();
                                _overlayKeys.assign(pool->key_begin(), pool->key_end());
                            }
                        }(), ...);
                    }, _pools);
                    for (const auto& [key, value] : tx->components) _overlayKeys.push_back(key.id);
                    std::sort(_overlayKeys.begin(), _overlayKeys.end());
                    _overlayKeys.erase(std::unique(_overlayKeys.begin(), _overlayKeys.end()), _overlayKeys.end());
                    _overlayMode = true;
                }
                _empty = false;
                _first = _overlayKeys.begin(); _last = _overlayKeys.end();
                return;
            }
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
            if (_overlayMode) return (bool(_scene->template lookupComponent<const std::remove_const_t<Cs>>(id)) && ...);
			return (std::get<PoolPtr<Cs>>(_pools)->contains(id) && ...);
		}

        mutable bool _overlayMode = false;
        mutable std::vector<Entity::ID> _overlayKeys;
		mutable Pools _pools;
		Scene* _scene;
		mutable DenseIt _first, _last;
		mutable bool _empty{ false };
	};

}

#include "component_registry.inl"
#include "entity_batch.inl"

#endif;
