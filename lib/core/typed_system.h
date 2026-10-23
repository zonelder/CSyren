#pragma once
// Typed systems: the parameters of execute() are the system's access declaration.
// There is no hand-written describe(): the frame graph reads the signature, and
// the compiler rejects any access the signature does not grant. Every scheduled
// system is a TypedSystem; lifecycle-only systems derive from OnceSystem.
//
//   class WeaponSystem : public core::TypedSystem<WeaponSystem>
//   {
//   public:
//       static constexpr auto phase = "simulation";
//       static constexpr std::array after{ "ViewModelSystem" };
//
//       void execute(core::Update<PilotState, WeaponInventory>::Where<PlayerTag> players,
//                    core::Select<physics::CharacterController> controllers,
//                    core::Spawn spawn,                 // grenades, impacts (prefabs)
//                    core::Res<const core::Time> time);
//   };
//
//   Select<Cs...>   read Cs. Iterating visits the entities that have every C (and
//                   every Where type); get<C>(id) reads one component of any entity.
//   Update<Cs...>   the same with write access. A const C inside Update is read-only.
//   ::Where<Ts...>  presence filter. Never conflicts with other systems.
//   Insert<Cs...>   create entities and add Cs. Other systems see them after the
//                   structural commit; never conflicts with readers or writers of Cs.
//   Delete          destroy entities and remove components (after the commit too).
//   Spawn           create copies of prefabs (core/spawn.h): types come from data,
//                   so none are declared; copies appear in the commit like Insert.
//   Res<T>          a service: Res<const T> reads, Res<T> writes. Empty (false) when
//                   the service is not registered.
//   Exclusive       the whole world: any component, structural changes and any
//                   service; conflicts with every task that declares an access.
//                   Only for systems whose types are known at run time (event
//                   dispatch, editor UI).
//
// Other libraries add parameter kinds (e.g. render::Recorder): a class with
//   static void describeParam(core::ParamAccess&);   // what it declares
//   static P makeParam(core::Scene&);                 // built before execute()
// (or makeParam() for a kind that does not use the scene). A system whose
// parameters do not use the scene can be run without a bound scene (in tests,
// system.run()); SystemManager schedules systems only with a scene.
//
// Any Select/Update also reads the entity hierarchy (entities()), which changes
// only in structural commits. Public static members set the schedule: phase
// (default "simulation"), after, before (a name or std::array of names), affinity,
// states (std::array of core::StateUse: CPU resource states, e.g. a frame token
// moving from "ready" to "submitted"), and for GPU recording tasks gpu (std::array
// of core::GpuUse), gpuScope, gpuQueue. Parameters are what execute() touches;
// static members only order it.
// Two systems that Insert the same type are ordered, so the later one sees the
// earlier commit; a same-phase reader not ordered after an inserter sees the
// new components only in the next frame.
//
// Lifecycle hooks (init, onAttach, onDetach, shutdown) run between frames with
// exclusive access and keep using scene()/Services directly.
#include <algorithm>
#include <array>
#include <concepts>
#include <memory>
#include <ranges>
#include <stdexcept>
#include <string>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <utility>
#include <vector>

#include "system_base.h"
#include "scene.h"
#include "entity_ref.h"
#include "services.h"

// IntelliSense (the EDG front end of Visual Studio, __INTELLISENSE__) only needs
// the parameter types and their members to show types and completions. The
// compile-only machinery (deducing the execute() signature, collecting the
// access declarations, uniqueness and parameter-kind checks) is hidden from it;
// the compiler and tools/tests/test_typed_contract.py still check everything.
#if defined(__INTELLISENSE__)
#define CSYREN_INTELLISENSE 1
#else
#define CSYREN_INTELLISENSE 0
#endif

// Wrap typed system classes (not their #includes) to make an unused execute()
// parameter, i.e. an unused access declaration, a compile error. IntelliSense
// does not report the warning anyway, so it sees empty markers.
#if defined(__INTELLISENSE__)
#define CS_STRICT_SYSTEMS_BEGIN
#define CS_STRICT_SYSTEMS_END
#elif defined(_MSC_VER) && !defined(__clang__)
#define CS_STRICT_SYSTEMS_BEGIN __pragma(warning(push)) __pragma(warning(1 : 4100)) __pragma(warning(error : 4100))
#define CS_STRICT_SYSTEMS_END __pragma(warning(pop))
#elif defined(__clang__) || defined(__GNUC__)
#define CS_STRICT_SYSTEMS_BEGIN _Pragma("GCC diagnostic push") _Pragma("GCC diagnostic error \"-Wunused-parameter\"")
#define CS_STRICT_SYSTEMS_END _Pragma("GCC diagnostic pop")
#else
#define CS_STRICT_SYSTEMS_BEGIN
#define CS_STRICT_SYSTEMS_END
#endif

namespace csyren::core
{
    template<class... Ts> struct TypeList {};

    // Access declarations of all parameters of one execute(), merged per type.
    // Parameter kinds add to it in describeParam().
    struct ParamAccess
    {
        struct Entry { TypeKey type; bool write; };
        std::vector<Entry> components, resources;
        std::vector<TypeKey> filters, inserts, localResources;
        bool structural = false;
        bool scene = false;
        bool exclusive = false; // Exclusive: access to everything, conflicts with every access
        bool gpu = false; // the task records GPU work: it must declare gpu resources

        // Component C (const C: read).
        template<class C> void component()
        { merge(components, TypeKey::of<C>(), !std::is_const_v<C>); scene = true; }
        // Service T (const T: read).
        template<class T> void resource()
        { merge(resources, TypeKey::of<T>(), !std::is_const_v<T>); }
        template<class C> void filter() { filters.push_back(TypeKey::of<C>()); scene = true; }
        template<class C> void insert() { inserts.push_back(TypeKey::of<C>()); structural = scene = true; }
        // Service T with task-local state (e.g. the Renderer of a GPU recording
        // task): reachable through Services::get without being a shared access.
        template<class T> void localResource() { addUnique(localResources, TypeKey::of<T>()); }

        void apply(TaskDescription& d) const
        {
            auto resourcesWithHierarchy = resources;
            if (scene) merge(resourcesWithHierarchy, TypeKey::of<SceneEntities>(), false);
            for (const auto& c : components)
                d.accesses.push_back({ c.type, true, c.write ? AccessMode::Write : AccessMode::Read, {}, {} });
            for (const auto& r : resourcesWithHierarchy)
                d.accesses.push_back({ r.type, false, r.write ? AccessMode::Write : AccessMode::Read, {}, {} });
            // A filter on a type that is also read adds nothing; neither does an
            // insert of a type that is also written (write access may add it).
            for (const auto& type : filters)
                if (!find(components, type) && !listed(d.filters, type)) d.filters.push_back(type);
            for (const auto& type : inserts)
            {
                const auto* c = find(components, type);
                if (!(c && c->write) && !listed(d.inserts, type)) d.inserts.push_back(type);
            }
            for (const auto& type : localResources) addUnique(d.localResources, type);
            d.structuralChanges = structural || exclusive;
            if (exclusive) d.exclusiveScene = d.dynamicResources = true;
        }
        // Resource states from a typed system's static `states` array; merged into
        // the access of the same service declared by a Res parameter, if any.
        static void applyState(TaskDescription& d, const StateUse& use)
        {
            for (auto& access : d.accesses)
                if (!access.component && access.type == use.type)
                {
                    if (use.mode == AccessMode::Write) access.mode = AccessMode::Write;
                    access.requiredState = use.required; access.resultingState = use.resulting;
                    return;
                }
            d.accesses.push_back({ use.type, false, use.mode, use.required, use.resulting });
        }
    private:
        static void merge(std::vector<Entry>& list, TypeKey type, bool write)
        {
            for (auto& entry : list) if (entry.type == type) { entry.write = entry.write || write; return; }
            list.push_back({ type, write });
        }
        static const Entry* find(const std::vector<Entry>& list, TypeKey type)
        {
            for (const auto& entry : list) if (entry.type == type) return &entry;
            return nullptr;
        }
        static bool listed(const std::vector<TypeKey>& list, TypeKey type)
        {
            return std::find(list.begin(), list.end(), type) != list.end();
        }
        static void addUnique(std::vector<TypeKey>& list, TypeKey type) { if (!listed(list, type)) list.push_back(type); }
    };

    // A parameter kind defined outside core.
    template<class P> concept SceneFreeSystemParam = requires { { P::makeParam() } -> std::same_as<P>; };
    template<class P> concept CustomSystemParam = requires(ParamAccess& access, Scene& scene) {
        P::describeParam(access);
        requires SceneFreeSystemParam<P> || requires { { P::makeParam(scene) } -> std::same_as<P>; };
    };

    namespace details
    {
        template<class T, class... Ts> inline constexpr bool contains_v = (std::is_same_v<T, Ts> || ...);
        template<class... Ts> struct Unique : std::true_type {};
        template<class T, class... Ts> struct Unique<T, Ts...>
            : std::bool_constant<!contains_v<T, Ts...> && Unique<Ts...>::value> {};
        template<class... Ts> inline constexpr bool unique_v = Unique<Ts...>::value;
        template<class> inline constexpr bool dependent_false = false;

        // The scene of the running task; null when none is bound (e.g. a system
        // executed directly in a test), which only scene parameters reject.
        inline Scene& requireScene(Scene* scene)
        {
            if (!scene) throw std::logic_error("Typed system: a scene parameter needs a bound scene (run it through SystemManager)");
            return *scene;
        }

        // describe(ParamAccess&) and make(Scene*) of every parameter kind; core kinds
        // are specialized below, others provide describeParam/makeParam.
        template<class P> struct Param
        {
#if !CSYREN_INTELLISENSE
            static_assert(CustomSystemParam<P>,
                "Typed system parameters must be Select, Update, Insert, Delete, Res, Exclusive or a type with "
                "static describeParam(ParamAccess&) / makeParam(Scene&) or makeParam() (passed by value)");
#endif
            static void describe(ParamAccess& access) { P::describeParam(access); }
            static P make(Scene* scene)
            {
                if constexpr (SceneFreeSystemParam<P>) return P::makeParam();
                else return P::makeParam(requireScene(scene));
            }
        };
    }

    // ------------------------------------------------------------------ Select / Update
    template<class Filters, class... Cs> class ComponentQuery;

    template<class... Ws, class... Cs>
    class ComponentQuery<TypeList<Ws...>, Cs...>
    {
#if !CSYREN_INTELLISENSE // compile-only checks (see CSYREN_INTELLISENSE)
        static_assert(sizeof...(Cs) > 0, "Select/Update needs at least one component type");
        static_assert(((!std::is_reference_v<Cs> && !std::is_volatile_v<Cs>) && ...), "Select/Update lists plain component types");
        static_assert(details::unique_v<std::remove_const_t<Cs>...>, "Select/Update lists a component type twice");
        static_assert(details::unique_v<std::remove_const_t<Cs>..., std::remove_const_t<Ws>...>,
            "Where<T>: T is listed twice or already selected");
#endif
        // Filter types lead, as in the classic view<const Tag, ...>() spelling: with
        // pools of equal size the first one decides the iteration order.
        using View = SceneView<const std::remove_const_t<Ws>..., Cs...>;
        template<class> friend struct details::Param;
        template<class, class...> friend class ComponentQuery;
    public:
        // Adds presence filters: iteration and contains() also require every T.
        template<class... Ts> using Where = ComponentQuery<TypeList<Ws..., Ts...>, Cs...>;
        using value_type = std::tuple<Entity::ID, Cs&...>;

        template<class T> static constexpr bool reads = details::contains_v<std::remove_const_t<T>, std::remove_const_t<Cs>...>;
        template<class T> static constexpr bool writes = details::contains_v<std::remove_const_t<T>, Cs...>;

        struct sentinel {};
        // Owns the loop's view snapshot in place: no allocation per loop. Not
        // copyable or movable (range-for and each() construct it in place).
        class iterator
        {
        public:
            using value_type = ComponentQuery::value_type;
            iterator(const iterator&) = delete;
            iterator& operator=(const iterator&) = delete;
            value_type operator*() const { return project(*_it, std::make_index_sequence<sizeof...(Cs)>{}); }
            iterator& operator++() { ++_it; return *this; }
            friend bool operator==(const iterator& it, sentinel) { return it._it == it._view.end(); }
            friend bool operator!=(const iterator& it, sentinel s) { return !(it == s); }
        private:
            friend class ComponentQuery;
            explicit iterator(Scene* scene) : _view(scene), _it(_view.begin()) {}
            // Drops the filter columns: (id, Ws&..., Cs&...) -> (id, Cs&...).
            template<class Tuple, size_t... I>
            static value_type project(Tuple&& row, std::index_sequence<I...>)
            { return value_type(std::get<0>(row), std::get<1 + sizeof...(Ws) + I>(row)...); }
            mutable View _view; // end() is non-const on SceneView
            typename View::iterator _it;
        };

        // Each loop takes a fresh snapshot (it sees components this task staged before it).
        iterator begin() const { validate(); return iterator(_scene); }
        sentinel end() const { return {}; }

        template<class Fn> void each(Fn&& fn) const
        {
            for (auto it = begin(); it != end(); ++it) std::apply(fn, *it);
        }

        // Entity has every listed component and every Where type.
        bool contains(Entity::ID id) const
        {
            validate();
            return View(_scene).contains(id);
        }
        bool contains(EntityRef reference) const { return contains(reference.id); }

        // One component of any entity; read-only unless T is listed without const.
        template<class T> auto get(Entity::ID id) const
        {
            using Value = std::remove_const_t<T>;
            static_assert(reads<Value>, "get<T>: T is not listed in this Select/Update");
            using Access = std::conditional_t<!std::is_const_v<T> && writes<Value>, Value, const Value>;
            return _scene->template getComponent<Access>(id);
        }
        // Through a reference kept in a component: empty when its entity is gone.
        template<class T> auto get(EntityRef reference) const { return get<T>(reference.id); }

        // Notify derived data (e.g. world transforms) after writing a component.
        template<class T> bool patch(Entity::ID id) const { requireWrite<T>(); return _scene->template patch<T>(id); }
        template<class T, class Edit> bool patch(Entity::ID id, Edit&& edit) const
        { requireWrite<T>(); return _scene->template patch<T>(id, std::forward<Edit>(edit)); }
        template<class T> bool patch(ComponentRef<T> component) const { requireWrite<T>(); return _scene->patch(component); }
        template<class T, class Edit> bool patch(ComponentRef<T> component, Edit&& edit) const
        { requireWrite<T>(); return _scene->patch(component, std::forward<Edit>(edit)); }

        // Every entity with T, whatever else it has (T must be listed here).
        template<class T> ComponentQuery<TypeList<>, const std::remove_const_t<T>> select() const
        {
            static_assert(reads<T>, "select<T>: T is not listed in this Select/Update");
            return ComponentQuery<TypeList<>, const std::remove_const_t<T>>(*_scene);
        }
        // Bumped on every structural change or patch() of T (for derived caches).
        template<class T> uint64_t componentRevision() const
        {
            static_assert(reads<T>, "componentRevision<T>: T is not listed in this Select/Update");
            return _scene->template componentRevision<const std::remove_const_t<T>>();
        }

        // The entity hierarchy is always readable; it changes only in structural commits.
        const EntitySet<Entity>& entities() const { return _scene->entities(); }
        uint64_t hierarchyRevision() const { return _scene->hierarchyRevision(); }
        // Distinguishes scenes (e.g. editor and play-mode instances) for derived caches.
        uint64_t sceneIdentity() const { return _scene->identity(); }

    private:
        explicit ComponentQuery(Scene& scene) : _scene(&scene) {}
        // Runtime validation backstop; the static checks above already hold.
        void validate() const
        {
            (_scene->template validateAccess<Cs>(), ...);
            (_scene->template validatePresence<Ws>(), ...);
        }
        template<class T> static constexpr void requireWrite()
        {
            static_assert(!std::is_const_v<T> && writes<T>, "patch<T>: T must be listed without const in an Update");
        }
        Scene* _scene;
    };

    template<class... Cs> using Select = ComponentQuery<TypeList<>, const std::remove_const_t<Cs>...>;
    template<class... Cs> using Update = ComponentQuery<TypeList<>, Cs...>;

    // QueryAccess<Q, T>::reads / ::writes: for constraining helpers that accept any
    // Select/Update (see worldMatrix in transform_hierarchy.h).
    template<class Q, class T> struct QueryAccess
    {
        static constexpr bool reads = false;
        static constexpr bool writes = false;
    };
    template<class... Ws, class... Cs, class T> struct QueryAccess<ComponentQuery<TypeList<Ws...>, Cs...>, T>
    {
        static constexpr bool reads = details::contains_v<std::remove_const_t<T>, std::remove_const_t<Cs>...>;
        static constexpr bool writes = details::contains_v<std::remove_const_t<T>, Cs...>;
    };

    // ------------------------------------------------------------------ Insert / Delete
    template<class... Cs>
    class Insert
    {
#if !CSYREN_INTELLISENSE
        static_assert(((!std::is_const_v<Cs> && !std::is_reference_v<Cs>) && ...), "Insert lists plain, non-const component types");
        static_assert(details::unique_v<Cs...>, "Insert lists a component type twice");
#endif
        template<class> friend struct details::Param;
    public:
        [[nodiscard]] Entity::ID create(std::string_view name = EntityManager::DEFAULT_NAME,
            Entity::ID parent = EntityManager::ROOT_PARENT) const
        {
            return _scene->createEntity(name, parent);
        }
        // The returned reference is writable immediately; others see it after the
        // commit. Use it within this execute() only: kept for later it would be an
        // undeclared live write.
        template<class T, class... Args> ComponentRef<T> add(Entity::ID id, Args&&... args) const
        {
            static_assert(details::contains_v<T, Cs...>, "add<T>: T is not listed in this Insert");
            return _scene->template addComponent<T>(id, std::forward<Args>(args)...);
        }
    private:
        explicit Insert(Scene& scene) : _scene(&scene) {}
        Scene* _scene;
    };

    class Delete
    {
        template<class> friend struct details::Param;
    public:
        void destroy(Entity::ID id) const { _scene->destroyEntity(id); }
        template<class T> void remove(Entity::ID id) const { _scene->removeComponent(id, reflection::ComponentFamily::getID<T>()); }
    private:
        explicit Delete(Scene& scene) : _scene(&scene) {}
        Scene* _scene;
    };

    // ------------------------------------------------------------------ Exclusive
    // The whole world for one task: any component, structural changes (staged
    // and committed like Insert/Delete) and any service (Services::get is not
    // validated). It conflicts with every task that declares an access, so no
    // such task runs concurrently with it.
    class Exclusive
    {
        template<class> friend struct details::Param;
    public:
        Scene& scene() const noexcept { return *_scene; }
        EventBus2& bus() const noexcept { return _scene->eventBus(); }
    private:
        explicit Exclusive(Scene& scene) : _scene(&scene) {}
        Scene* _scene;
    };

    // ------------------------------------------------------------------ Res
    template<class T>
    class Res
    {
        static_assert(!std::is_reference_v<T> && !std::is_pointer_v<T>, "Res<T> takes a service type");
        static_assert(!std::is_same_v<std::remove_const_t<T>, Scene>, "Res<Scene>: use Select/Update/Insert/Delete for scene data");
        template<class> friend struct details::Param;
    public:
        T* get() const noexcept { return _service; }
        T* operator->() const { return &**this; }
        T& operator*() const
        {
            if (!_service) throw std::logic_error("Res<T>: service is not registered: " + std::string(typeName<std::remove_const_t<T>>()));
            return *_service;
        }
        explicit operator bool() const noexcept { return _service != nullptr; }
    private:
        explicit Res(T* service) : _service(service) {}
        T* _service;
    };

    namespace details
    {
        template<class... Ws, class... Cs> struct Param<ComponentQuery<TypeList<Ws...>, Cs...>>
        {
            static void describe(ParamAccess& access) { (access.component<Cs>(), ...); (access.filter<Ws>(), ...); }
            static ComponentQuery<TypeList<Ws...>, Cs...> make(Scene* scene) { return ComponentQuery<TypeList<Ws...>, Cs...>(requireScene(scene)); }
        };
        template<class... Cs> struct Param<Insert<Cs...>>
        {
            static void describe(ParamAccess& access) { (access.insert<Cs>(), ...); access.structural = access.scene = true; }
            static Insert<Cs...> make(Scene* scene) { return Insert<Cs...>(requireScene(scene)); }
        };
        template<> struct Param<Delete>
        {
            static void describe(ParamAccess& access) { access.structural = access.scene = true; }
            static Delete make(Scene* scene) { return Delete(requireScene(scene)); }
        };
        template<> struct Param<Exclusive>
        {
            static void describe(ParamAccess& access) { access.exclusive = true; }
            static Exclusive make(Scene* scene) { return Exclusive(requireScene(scene)); }
        };
        template<class T> struct Param<Res<T>>
        {
            static void describe(ParamAccess& access) { access.resource<T>(); }
            static Res<T> make(Scene*) { return Res<T>(Services::find<T>()); }
        };

        template<class F> struct ExecuteSignature
        {
            static_assert(dependent_false<F>, "Typed system: declare a public, non-const 'void execute(params...)'");
        };
        template<class C, class... Ps> struct ExecuteSignature<void (C::*)(Ps...)>
        {
            using Class = C;
#if !CSYREN_INTELLISENSE
            static_assert(((!std::is_lvalue_reference_v<Ps> || std::is_const_v<std::remove_reference_t<Ps>>) && ...),
                "Typed system parameters are passed by value");
#endif
            static void describe(ParamAccess& access) { (Param<std::remove_cvref_t<Ps>>::describe(access), ...); }
            template<class Self> static void invoke(Self& self, Scene* scene)
            {
                self.execute(Param<std::remove_cvref_t<Ps>>::make(scene)...);
            }
        };
        template<class C, class... Ps> struct ExecuteSignature<void (C::*)(Ps...) noexcept> : ExecuteSignature<void (C::*)(Ps...)> {};

        inline std::vector<std::string> names(const char* name) { return { name }; }
        inline std::vector<std::string> names(std::string_view name) { return { std::string(name) }; }
        template<class Range> requires std::ranges::input_range<const Range&>
        std::vector<std::string> names(const Range& range)
        {
            std::vector<std::string> result;
            for (const auto& name : range) result.emplace_back(name);
            return result;
        }
    }

    // ------------------------------------------------------------------ TypedSystem
    template<class Derived> class TypedSystem;
    namespace details
    {
        template<class Derived> TaskDescription describeTyped();
        template<class Derived> void runTyped(System& system);
    }

    template<class Derived>
    class TypedSystem : public System
    {
    public:
        // Defaults; a system redeclares the ones it needs (publicly: a private
        // redeclaration is an access error instead of being silently ignored).
        static constexpr const char* phase = "simulation";
        static constexpr std::array<const char*, 0> after{};
        static constexpr std::array<const char*, 0> before{};
        static constexpr TaskAffinity affinity = TaskAffinity::Worker;
        // CPU resource states (StateUse::read / StateUse::write).
        static constexpr std::array<StateUse, 0> states{};
        // GPU recording tasks: resources with the state each one must be in, the
        // per-frame or per-view scope and the queue (see docs/frame_graph.md).
        static constexpr std::array<GpuUse, 0> gpu{};
        static constexpr GpuTaskScope gpuScope = GpuTaskScope::Frame;
        static constexpr const char* gpuQueue = "graphics";

        bool scheduled() const noexcept final { return true; }
        TaskDescription describe() const final { return _describe(); }

    protected:
        // The work of describe() and run() is bound here, where the system class is
        // complete: the base class itself never needs it (it is instantiated while
        // the system class is still being declared).
        TypedSystem() noexcept : _describe(&details::describeTyped<Derived>), _run(&details::runTyped<Derived>) {}

    private:
        void runTask() final { _run(*this); }
        TaskDescription (*_describe)();
        void (*_run)(System&);
    };

    namespace details
    {
        template<class Derived> TaskDescription describeTyped()
        {
#if CSYREN_INTELLISENSE
            return {};
#else
            using Signature = ExecuteSignature<decltype(&Derived::execute)>;
            static_assert(!std::is_same_v<typename Signature::Class, TypedSystem<Derived>> && std::is_base_of_v<typename Signature::Class, Derived>,
                "Typed system: declare 'void execute(params...)' in the system class");
            TaskDescription d;
            d.phase = std::string(Derived::phase);
            d.affinity = Derived::affinity;
            d.after = names(Derived::after);
            d.before = names(Derived::before);
            for (const GpuUse& use : Derived::gpu) d.gpu(use.resource, use.state, use.mode);
            d.gpuScope = Derived::gpuScope;
            d.gpuQueue = Derived::gpuQueue;
            ParamAccess access;
            Signature::describe(access);
            access.apply(d);
            for (const StateUse& use : Derived::states) ParamAccess::applyState(d, use);
            if (access.gpu && d.gpuAccesses.empty())
                throw std::logic_error("Typed system records GPU work but declares no gpu resources: " + std::string(typeName<Derived>()));
            return d;
#endif
        }
        template<class Derived> void runTyped(System& system)
        {
#if CSYREN_INTELLISENSE
            (void)system;
#else
            using Signature = ExecuteSignature<decltype(&Derived::execute)>;
            Signature::invoke(static_cast<Derived&>(system), Services::find<Scene>());
#endif
        }
    }
}
