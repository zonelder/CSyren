#include "pch.h"
#include "core/typed_system.h"
#include "core/frame_graph.h"
#include "core/system_manager.h"
#include "core/scene_context.h"
#include "scene_job_test_access.h"
#include <array>

namespace {
using namespace csyren::core;
using TypedAccess = details::SceneJobTestAccess;

struct TPosition { int x = 0; };
struct TVelocity { int x = 0; };
struct TTag {};
struct TConfig { int scale = 3; };
struct TCounter { int sum = 0; int frames = 0; bool sawMissing = true; };
struct TMissing {};

void registerTypedComponents() {
    REGISTER_COMPONENT(TPosition); REGISTER_COMPONENT(TVelocity); REGISTER_COMPONENT(TTag);
}

const TaskAccess* findAccess(const TaskDescription& d, TypeKey type, bool component) {
    for (const auto& a : d.accesses) if (a.type == type && a.component == component) return &a;
    return nullptr;
}
bool listed(const std::vector<TypeKey>& list, TypeKey type) {
    return std::find(list.begin(), list.end(), type) != list.end();
}

// Registers Scene (+ bus) as services and always cleans the registry up.
struct ServiceScene {
    EventBus2& bus = details::ServiceRegistry::create<EventBus2>();
    Scene& scene = details::ServiceRegistry::create<Scene>(&bus);
    ~ServiceScene() { details::ServiceRegistry::shutdownAll(); }
};

// Spawns one tagged mover and one untagged entity on the first frame.
class TSpawnSystem : public TypedSystem<TSpawnSystem> {
public:
    static constexpr auto phase = "custom";
    void execute(Insert<TPosition, TVelocity, TTag> spawn, Res<TCounter> counter) {
        if (counter->frames++ > 0) return;
        const auto mover = spawn.create("mover");
        spawn.add<TPosition>(mover, 0); spawn.add<TVelocity>(mover, 2); spawn.add<TTag>(mover);
        const auto still = spawn.create("still");
        spawn.add<TPosition>(still, 100); spawn.add<TVelocity>(still, 5);
    }
};
class TMoveSystem : public TypedSystem<TMoveSystem> {
public:
    static constexpr auto phase = "custom";
    static constexpr std::array after{ "TSpawnSystem" };
    void execute(Update<TPosition, const TVelocity>::Where<TTag> movers, Res<const TConfig> config) {
        for (auto [id, position, velocity] : movers) position.x += velocity.x * config->scale;
    }
};
class TCleanupSystem : public TypedSystem<TCleanupSystem> {
public:
    static constexpr auto phase = "custom";
    static constexpr auto after = "TMoveSystem";
    void execute(Select<TPosition> positions, Delete remove) {
        for (auto [id, position] : positions) {
            if (position.x >= 100) remove.destroy(id);
            else remove.remove<TVelocity>(id); // movers stop after this frame
        }
    }
};
class TObserverSystem : public TypedSystem<TObserverSystem> {
public:
    static constexpr auto phase = "custom";
    static constexpr std::array after{ "TCleanupSystem" };
    void execute(Select<TPosition> positions, Res<TCounter> counter, Res<const TMissing> missing) {
        counter->sum = 0;
        positions.each([&](Entity::ID, const TPosition& position) { counter->sum += position.x; });
        counter->sawMissing = static_cast<bool>(missing);
    }
};
REGISTER_SYSTEM(TSpawnSystem)
REGISTER_SYSTEM(TMoveSystem)
REGISTER_SYSTEM(TCleanupSystem)
REGISTER_SYSTEM(TObserverSystem)

TEST(TypeKey, IdentifiesTypesWithoutRtti) {
    static_assert(TypeKey::of<TPosition>() == TypeKey::of<const TPosition&>());
    static_assert(!(TypeKey::of<TPosition>() == TypeKey::of<TVelocity>()));
    constexpr auto key = TypeKey::of<TPosition>();
    EXPECT_TRUE(static_cast<bool>(key));
    EXPECT_FALSE(static_cast<bool>(TypeKey{}));
    EXPECT_NE(key.name.find("TPosition"), std::string_view::npos);
    EXPECT_EQ(typeName<int>(), "int");
    EXPECT_EQ(typeName<csyren::core::SceneEntities>(), "csyren::core::SceneEntities");
    std::unordered_map<TypeKey, int> map{ {TypeKey::of<TPosition>(), 1}, {TypeKey::of<TVelocity>(), 2} };
    EXPECT_EQ(map.at(TypeKey::of<const TVelocity>()), 2);
}

TEST(TypedSystem, SignatureIsTheDeclaration) {
    const auto d = TMoveSystem{}.describe();
    EXPECT_EQ(d.phase, "custom");
    EXPECT_EQ(d.after, std::vector<std::string>{"TSpawnSystem"});
    EXPECT_TRUE(d.before.empty());
    EXPECT_EQ(d.affinity, TaskAffinity::Worker);
    ASSERT_TRUE(findAccess(d, TypeKey::of<TPosition>(), true));
    EXPECT_EQ(findAccess(d, TypeKey::of<TPosition>(), true)->mode, AccessMode::Write);
    ASSERT_TRUE(findAccess(d, TypeKey::of<TVelocity>(), true));
    EXPECT_EQ(findAccess(d, TypeKey::of<TVelocity>(), true)->mode, AccessMode::Read);
    ASSERT_TRUE(findAccess(d, TypeKey::of<TConfig>(), false));
    EXPECT_EQ(findAccess(d, TypeKey::of<TConfig>(), false)->mode, AccessMode::Read);
    ASSERT_TRUE(findAccess(d, TypeKey::of<SceneEntities>(), false)); // the hierarchy is always readable
    EXPECT_FALSE(findAccess(d, TypeKey::of<TTag>(), true));
    EXPECT_EQ(d.filters, std::vector<TypeKey>{TypeKey::of<TTag>()});
    EXPECT_TRUE(d.inserts.empty());
    EXPECT_FALSE(d.structuralChanges);

    const auto spawn = TSpawnSystem{}.describe();
    EXPECT_TRUE(spawn.structuralChanges);
    EXPECT_EQ(spawn.inserts.size(), 3u);
    EXPECT_FALSE(findAccess(spawn, TypeKey::of<TPosition>(), true)); // inserting is not writing
    EXPECT_EQ(findAccess(spawn, TypeKey::of<TCounter>(), false)->mode, AccessMode::Write);
    EXPECT_TRUE(TCleanupSystem{}.describe().structuralChanges);
    EXPECT_EQ(TCleanupSystem{}.describe().after, std::vector<std::string>{"TMoveSystem"});
}

class TMergeSystem : public TypedSystem<TMergeSystem> {
public:
    static constexpr auto affinity = TaskAffinity::MainThread;
    static constexpr std::array before{ "a", "b" };
    void execute(Select<TPosition, TVelocity> read, Update<TPosition>::Where<TVelocity, TTag> write,
        Insert<TPosition, TTag> spawn, Res<const TConfig> config, Res<TConfig> mutableConfig) {
        (void)read; (void)write; (void)spawn; (void)config; (void)mutableConfig;
    }
};
TEST(TypedSystem, DeclarationsMergePerType) {
    const auto d = TMergeSystem{}.describe();
    EXPECT_EQ(d.phase, "simulation");
    EXPECT_EQ(d.affinity, TaskAffinity::MainThread);
    EXPECT_EQ(d.before, (std::vector<std::string>{"a", "b"}));
    size_t positions = 0;
    for (const auto& a : d.accesses) if (a.component && a.type == TypeKey::of<TPosition>()) ++positions;
    EXPECT_EQ(positions, 1u);
    EXPECT_EQ(findAccess(d, TypeKey::of<TPosition>(), true)->mode, AccessMode::Write);
    EXPECT_EQ(findAccess(d, TypeKey::of<TConfig>(), false)->mode, AccessMode::Write);
    EXPECT_FALSE(listed(d.filters, TypeKey::of<TVelocity>())); // read anyway
    EXPECT_TRUE(listed(d.filters, TypeKey::of<TTag>()));
    EXPECT_FALSE(listed(d.inserts, TypeKey::of<TPosition>())); // write access may add it
    EXPECT_TRUE(listed(d.inserts, TypeKey::of<TTag>()));
    EXPECT_TRUE(d.structuralChanges);
}

// A parameter kind from outside core (like render::Recorder): task-local service.
struct TRecorder {
    TConfig* config;
    static void describeParam(ParamAccess& access) { access.localResource<TConfig>(); access.gpu = true; }
    static TRecorder makeParam(Scene&) { return { Services::get<TConfig>() }; }
};
class TGpuPass : public TypedSystem<TGpuPass> {
public:
    static constexpr auto phase = "render";
    static constexpr auto gpuScope = GpuTaskScope::View;
    static constexpr std::array gpu{ GpuUse{ "scene.hdr", 4u, AccessMode::Write }, GpuUse{ "scene.depth", 8u, AccessMode::Read } };
    void execute(TRecorder recorder, Select<TPosition> positions) { (void)recorder; (void)positions; }
};
class TGpuPassWithoutResources : public TypedSystem<TGpuPassWithoutResources> {
public:
    void execute(TRecorder recorder) { (void)recorder; }
};
TEST(TypedSystem, CustomParametersAndGpuDeclarations) {
    const auto d = TGpuPass{}.describe();
    EXPECT_EQ(d.phase, "render");
    EXPECT_EQ(d.gpuScope, GpuTaskScope::View);
    EXPECT_EQ(d.gpuQueue, "graphics");
    ASSERT_EQ(d.gpuAccesses.size(), 2u);
    EXPECT_EQ(d.gpuAccesses[0].resource, "scene.hdr");
    EXPECT_EQ(d.gpuAccesses[0].requiredState, 4u);
    EXPECT_EQ(d.gpuAccesses[1].mode, AccessMode::Read);
    EXPECT_EQ(d.localResources, std::vector<TypeKey>{TypeKey::of<TConfig>()});
    EXPECT_FALSE(findAccess(d, TypeKey::of<TConfig>(), false)); // local, not a shared access
    EXPECT_THROW(TGpuPassWithoutResources{}.describe(), std::logic_error);
}

// Parameters that do not use the scene (Res, makeParam() without arguments)
// let a system run with no scene bound, e.g. called directly in a test.
struct TSceneFreeParam {
    TConfig* config;
    static void describeParam(ParamAccess& access) { access.localResource<TConfig>(); }
    static TSceneFreeParam makeParam() { return { Services::get<TConfig>() }; }
};
class TServiceOnlySystem : public TypedSystem<TServiceOnlySystem> {
public:
    void execute(TSceneFreeParam param, Res<TCounter> counter) { counter->sum += param.config->scale; }
};
class TSceneQuerySystem : public TypedSystem<TSceneQuerySystem> {
public:
    void execute(Select<TPosition> positions) { (void)positions; }
};
TEST(TypedSystem, RunsWithoutASceneWhenNoParameterUsesIt) {
    struct Cleanup { ~Cleanup() { details::ServiceRegistry::shutdownAll(); } } cleanup;
    details::ServiceRegistry::create<TConfig>();
    auto& counter = details::ServiceRegistry::create<TCounter>();
    ASSERT_EQ(Services::find<Scene>(), nullptr);
    TServiceOnlySystem services;
    EXPECT_NO_THROW(services.run());
    EXPECT_EQ(counter.sum, 3);
    TSceneQuerySystem queries;
    EXPECT_THROW(queries.run(), std::logic_error);
}

// Exclusive: the whole world for one task (exclusive scene, dynamic resources,
// structural changes); its tasks conflict with every other task.
struct TFrame {};
class TExclusiveSystem : public TypedSystem<TExclusiveSystem> {
public:
    static constexpr auto phase = "custom";
    inline static int ran = 0;
    void execute(Exclusive world) {
        auto id = world.scene().createEntity(); world.scene().addComponent<TPosition>(id)->x = 5;
        (void)Services::get<TConfig>(); // any service, without a declaration
        ++ran;
    }
};
// states: CPU resource states, merged with a Res access of the same service.
class TStateProducer : public TypedSystem<TStateProducer> {
public:
    static constexpr auto phase = "custom";
    static constexpr std::array states{ StateUse::write<TFrame>("", "ready") };
    void execute(Res<const TConfig> config) { (void)config; }
};
class TStateConsumer : public TypedSystem<TStateConsumer> {
public:
    static constexpr auto phase = "custom";
    static constexpr std::array states{ StateUse::write<TConfig>("ready", "done") };
    void execute(Res<const TConfig> config) { (void)config; }
};
TEST(TypedSystem, ExclusiveAndStates) {
    const auto exclusive = TExclusiveSystem{}.describe();
    EXPECT_TRUE(exclusive.exclusiveScene && exclusive.dynamicResources && exclusive.structuralChanges);
    EXPECT_TRUE(exclusive.accesses.empty());

    const auto producer = TStateProducer{}.describe();
    const auto* frame = findAccess(producer, TypeKey::of<TFrame>(), false);
    ASSERT_TRUE(frame);
    EXPECT_EQ(frame->mode, AccessMode::Write);
    EXPECT_EQ(frame->requiredState, "");
    EXPECT_EQ(frame->resultingState, "ready");
    // Merged into the Res<const TConfig> access (one access per type).
    const auto consumer = TStateConsumer{}.describe();
    size_t configs = 0;
    for (const auto& a : consumer.accesses) if (a.type == TypeKey::of<TConfig>()) ++configs;
    EXPECT_EQ(configs, 1u);
    const auto* config = findAccess(consumer, TypeKey::of<TConfig>(), false);
    ASSERT_TRUE(config);
    EXPECT_EQ(config->mode, AccessMode::Write);
    EXPECT_EQ(config->requiredState, "ready");
    EXPECT_EQ(config->resultingState, "done");

    // A unique producer becomes a dependency even when registered later.
    FrameGraph graph; graph.phase({"custom"});
    TaskDescription reader; reader.phase = "custom"; reader.readResource<TFrame>("ready");
    graph.task("reader", reader, []{}); graph.task("producer", producer, []{});
    graph.task("exclusive", exclusive, []{});
    graph.compile();
    EXPECT_TRUE(graph.dependsOn("reader", "producer"));
    EXPECT_TRUE(graph.dependsOn("exclusive", "reader") || graph.dependsOn("reader", "exclusive"));
}

// OnceSystem: lifecycle only, never scheduled.
class TSetupSystem : public OnceSystem {
public:
    inline static int initialized = 0, shut = 0;
    void init() override { ++initialized; }
    void shutdown() override { ++shut; }
};
REGISTER_SYSTEM(TSetupSystem)
REGISTER_SYSTEM(TExclusiveSystem)
TEST(TypedSystem, OnceSystemsStayOutOfTheGraph) {
    registerTypedComponents();
    ServiceScene services;
    details::ServiceRegistry::create<TConfig>();
    details::ServiceRegistry::initializeAll();
    TSetupSystem::initialized = TSetupSystem::shut = 0; TExclusiveSystem::ran = 0;
    SystemManager systems; systems.phase({"custom"});
    systems.add("TSetupSystem"); systems.add("TExclusiveSystem");
    systems.init();
    EXPECT_EQ(TSetupSystem::initialized, 1);
    EXPECT_FALSE(TSetupSystem{}.scheduled());
    systems.execute();
    EXPECT_EQ(TExclusiveSystem::ran, 1);
    bool once = false;
    for (const auto& task : systems.executionGraph().tasks) once = once || task.task.name == "TSetupSystem";
    EXPECT_FALSE(once);
    EXPECT_EQ(systems.list().size(), 2u); // still configured (shown as init-only)
    size_t positions = 0;
    for (auto [id, position] : services.scene.view<const TPosition>()) { (void)id; positions += position.x == 5; }
    EXPECT_EQ(positions, 1u); // the exclusive task's structural change was committed
    systems.shutdown();
    EXPECT_EQ(TSetupSystem::shut, 1);
}

TEST(TypedSystem, InsertsAndFiltersDoNotConflictWithLiveAccess) {
    FrameGraph graph; graph.phase({"custom"});
    TaskDescription inserter; inserter.phase = "custom"; inserter.insert<TPosition>();
    TaskDescription writer; writer.phase = "custom"; writer.write<TPosition>().write<TTag>();
    TaskDescription filtered; filtered.phase = "custom"; filtered.read<TVelocity>().filter<TTag>();
    graph.task("inserter", inserter, []{}); graph.task("writer", writer, []{}); graph.task("filtered", filtered, []{});
    graph.compile();
    EXPECT_FALSE(graph.dependsOn("writer", "inserter")); EXPECT_FALSE(graph.dependsOn("inserter", "writer"));
    EXPECT_FALSE(graph.dependsOn("filtered", "writer")); EXPECT_FALSE(graph.dependsOn("writer", "filtered"));
    EXPECT_TRUE(inserter.structuralChanges);
    EXPECT_THROW(inserter.insert<TPosition>(), std::invalid_argument);
}

TEST(TypedSystem, InsertsOfTheSameTypeAreOrdered) {
    FrameGraph graph; graph.phase({"custom"});
    TaskDescription a; a.phase = "custom"; a.insert<TTag>();
    TaskDescription b; b.phase = "custom"; b.insert<TTag>();
    TaskDescription classic; classic.phase = "custom"; classic.write<TTag>(); classic.structuralChanges = true;
    graph.task("a", a, []{}); graph.task("b", b, []{}); graph.task("classic", classic, []{});
    graph.compile();
    EXPECT_TRUE(graph.dependsOn("b", "a"));
    EXPECT_TRUE(graph.dependsOn("classic", "b")); // a structural writer may add the type too

    FrameGraph live; live.phase({"custom"});
    TaskDescription liveWriter; liveWriter.phase = "custom"; liveWriter.write<TTag>(); // no structural changes
    live.task("a", a, []{}); live.task("live", liveWriter, []{});
    live.compile();
    EXPECT_FALSE(live.dependsOn("live", "a")); EXPECT_FALSE(live.dependsOn("a", "live"));
}

TEST(TypedSystem, RunsThroughTheFrameGraph) {
    registerTypedComponents();
    ServiceScene services;
    details::ServiceRegistry::create<TConfig>();
    auto& counter = details::ServiceRegistry::create<TCounter>();
    details::ServiceRegistry::initializeAll();
    SystemManager systems; systems.phase({"custom"});
    systems.jobs().init(4);
    // Registration order is reversed on purpose: explicit order and commits decide.
    systems.add("TObserverSystem"); systems.add("TCleanupSystem"); systems.add("TMoveSystem"); systems.add("TSpawnSystem");
    systems.init();
    systems.execute();
    // Spawned this frame, committed before TMoveSystem: 0 + 2 * 3. The untagged
    // entity is not moved and is destroyed before the observer runs.
    EXPECT_EQ(counter.sum, 6);
    EXPECT_FALSE(counter.sawMissing);
    EXPECT_FALSE(systems.executing());
    systems.execute();
    EXPECT_EQ(counter.sum, 6); // TVelocity was removed, so the mover no longer matches
    size_t alive = 0, moving = 0;
    for (auto [id, position] : services.scene.view<const TPosition>()) { (void)position; ++alive; }
    for (auto [id, velocity] : services.scene.view<const TVelocity>()) { (void)velocity; ++moving; }
    EXPECT_EQ(alive, 1u);
    EXPECT_EQ(moving, 0u);
    systems.shutdown();
}

// Two unordered systems tag the same existing entities, each skipping entities
// that are already tagged. The second one must see the first one's commit.
template<class Derived> class TTagger : public TypedSystem<Derived> {
public:
    static constexpr auto phase = "custom";
    void execute(Select<TPosition> positions, Select<TTag> tagged, Insert<TTag> tag) {
        for (auto [id, position] : positions) { (void)position; if (!tagged.contains(id)) tag.add<TTag>(id); }
    }
};
class TTaggerA : public TTagger<TTaggerA> {};
class TTaggerB : public TTagger<TTaggerB> {};
REGISTER_SYSTEM(TTaggerA)
REGISTER_SYSTEM(TTaggerB)
TEST(TypedSystem, SameTypeInsertsIntoExistingEntitiesDoNotCollide) {
    registerTypedComponents();
    ServiceScene services;
    details::ServiceRegistry::initializeAll();
    for (int i = 0; i < 3; ++i) services.scene.addComponent<TPosition>(services.scene.createEntity(), i);
    SystemManager systems; systems.phase({"custom"});
    systems.jobs().init(4);
    systems.add("TTaggerA"); systems.add("TTaggerB"); systems.init();
    EXPECT_NO_THROW(systems.execute());
    size_t tagged = 0;
    for (auto [id, tag] : services.scene.view<const TTag>()) { (void)tag; ++tagged; }
    EXPECT_EQ(tagged, 3u);
    systems.shutdown();
}

class TLifecycleSystem : public TypedSystem<TLifecycleSystem> {
public:
    static constexpr auto phase = "custom";
    inline static int rejected = 0;
    void execute(Res<SystemManager> manager) {
        rejected = 0;
        auto attempt = [&](auto&& edit) { try { edit(); } catch (const std::logic_error&) { ++rejected; } };
        attempt([&] { manager->add("TObserverSystem"); });
        attempt([&] { manager->remove("TLifecycleSystem"); });
        attempt([&] { manager->reserve(16); });
        attempt([&] { manager->unbind(); });
        attempt([&] { manager->init(); });
        attempt([&] { manager->shutdown(); });
        attempt([&] { manager->execute(); });
        attempt([&] { manager->phase({"late"}); });
        attempt([&] { manager->taskBackend(nullptr); });
        attempt([&] { manager->processMove(); });
    }
};
REGISTER_SYSTEM(TLifecycleSystem)
TEST(TypedSystem, LifecycleChangesAreRejectedWhileExecuting) {
    ServiceScene services;
    auto& systems = details::ServiceRegistry::create<SystemManager>();
    details::ServiceRegistry::initializeAll();
    systems.phase({"custom"}); systems.add("TLifecycleSystem"); systems.init();
    EXPECT_NO_THROW(systems.execute());
    EXPECT_EQ(TLifecycleSystem::rejected, 10);
    EXPECT_FALSE(systems.executing());
    EXPECT_NO_THROW(systems.remove("TLifecycleSystem"));
    systems.shutdown();
}

TEST(TypedSystem, InsertGrantsAddingButNotReading) {
    registerTypedComponents();
    SceneInstance instance; auto& scene = instance.scene();
    const auto existing = scene.createEntity(); scene.addComponent<TPosition>(existing, 5);
    TypedAccess::Transaction tx;
    TaskDescription d; d.insert<TPosition>();
    Entity::ID added = Entity::invalidID;
    {
        TypedAccess::TaskScope scope(scene, &tx, &d);
        added = scene.createEntity();
        EXPECT_NO_THROW(scene.addComponent<TPosition>(added, 1));
#if CSYREN_ACCESS_VALIDATION
        EXPECT_THROW(scene.addComponent<TVelocity>(added), std::logic_error);
        EXPECT_THROW(scene.getComponent<const TPosition>(existing), std::logic_error);
#endif
    }
    EXPECT_FALSE(scene.hasComponent<TPosition>(added)); // staged until the commit
    tx.apply(scene);
    EXPECT_EQ(scene.getComponent<const TPosition>(added)->x, 1);
}

#if CSYREN_ACCESS_VALIDATION
TEST(TypedSystem, FiltersAllowPresenceTestsOnly) {
    registerTypedComponents();
    SceneInstance instance; auto& scene = instance.scene();
    TaskDescription filter; filter.read<TVelocity>().filter<TPosition>();
    TypedAccess::TaskScope scope(scene, nullptr, &filter);
    EXPECT_NO_THROW(scene.validatePresence<TPosition>());
    EXPECT_THROW((scene.view<const TVelocity, const TPosition>()), std::logic_error);
    EXPECT_THROW(scene.getComponent<const TPosition>(0), std::logic_error);
    EXPECT_THROW(scene.validatePresence<TTag>(), std::logic_error);
}
#endif
}
