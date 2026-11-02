#include "pch.h"
#include "core/typed_system.h"
#include "core/commands.h"
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
    EventBus& bus = details::ServiceRegistry::create<EventBus>();
    Scene& scene = details::ServiceRegistry::create<Scene>(&bus);
    ~ServiceScene() { details::ServiceRegistry::shutdownAll(); }
};

// Spawns one tagged mover and one untagged entity on the first frame.
class TSpawnSystem : public TypedSystem<TSpawnSystem> {
public:
    static constexpr auto phase = "custom";
    void execute(Commands commands, Res<TCounter> counter) {
        if (counter->frames++ > 0) return;
        commands.create("mover").with(TPosition{ 0 }).with(TVelocity{ 2 }).with(TTag{});
        commands.create("still").with(TPosition{ 100 }).with(TVelocity{ 5 });
    }
};
class TMoveSystem : public TypedSystem<TMoveSystem> {
public:
    static constexpr auto phase = "custom";
    static constexpr std::array after{ "TSpawnSystem" };
    void execute(Query<TPosition, const TVelocity>::With<TTag> movers, Res<const TConfig> config) {
        for (auto [id, position, velocity] : movers) position.x += velocity.x * config->scale;
    }
};
class TCleanupSystem : public TypedSystem<TCleanupSystem> {
public:
    static constexpr auto phase = "custom";
    static constexpr auto after = "TMoveSystem";
    void execute(Query<const TPosition> positions, Commands commands) {
        for (auto [id, position] : positions) {
            if (position.x >= 100) commands.entity(id).destroy();
            else commands.entity(id).remove<TVelocity>(); // movers stop after this frame
        }
    }
};
class TObserverSystem : public TypedSystem<TObserverSystem> {
public:
    static constexpr auto phase = "custom";
    static constexpr std::array after{ "TCleanupSystem" };
    void execute(Query<const TPosition> positions, Res<TCounter> counter, Res<const TMissing> missing) {
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
    EXPECT_FALSE(d.structuralChanges);

    const auto spawn = TSpawnSystem{}.describe();
    EXPECT_TRUE(spawn.structuralChanges);
    for (const auto& access : spawn.accesses) EXPECT_FALSE(access.component); // Commands declare no types
    EXPECT_EQ(findAccess(spawn, TypeKey::of<TCounter>(), false)->mode, AccessMode::Write);
    EXPECT_TRUE(TCleanupSystem{}.describe().structuralChanges);
    EXPECT_EQ(TCleanupSystem{}.describe().after, std::vector<std::string>{"TMoveSystem"});
}

class TMergeSystem : public TypedSystem<TMergeSystem> {
public:
    static constexpr auto affinity = TaskAffinity::MainThread;
    static constexpr std::array before{ "a", "b" };
    void execute(Query<const TPosition, const TVelocity> read, Query<TPosition>::With<TVelocity, TTag> write,
        Commands commands, Res<const TConfig> config, Res<TConfig> mutableConfig) {
        (void)read; (void)write; (void)commands; (void)config; (void)mutableConfig;
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
    void execute(TRecorder recorder, Query<const TPosition> positions) { (void)recorder; (void)positions; }
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
    void execute(Query<const TPosition> positions) { (void)positions; }
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

TEST(TypedSystem, CommandsAndFiltersDoNotConflictWithLiveAccess) {
    FrameGraph graph; graph.phase({"custom"});
    auto structural = TSpawnSystem{}.describe(); structural.phase = "custom";
    TaskDescription writer; writer.phase = "custom"; writer.write<TPosition>().write<TTag>();
    TaskDescription filtered; filtered.phase = "custom"; filtered.read<TVelocity>().filter<TTag>();
    graph.task("structural", structural, []{}); graph.task("writer", writer, []{}); graph.task("filtered", filtered, []{});
    graph.compile();
    EXPECT_FALSE(graph.dependsOn("writer", "structural")); EXPECT_FALSE(graph.dependsOn("structural", "writer"));
    EXPECT_FALSE(graph.dependsOn("filtered", "writer")); EXPECT_FALSE(graph.dependsOn("writer", "filtered"));
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

// Two unordered systems tag the same existing entities. with() adds a component
// only when the entity has none, so the later commit skips the tagged entities.
template<class Derived> class TTagger : public TypedSystem<Derived> {
public:
    static constexpr auto phase = "custom";
    void execute(Query<const TPosition> positions, Commands commands) {
        for (auto [id, position] : positions) { (void)position; commands.entity(id).with(TTag{}); }
    }
};
class TTaggerA : public TTagger<TTaggerA> {};
class TTaggerB : public TTagger<TTaggerB> {};
REGISTER_SYSTEM(TTaggerA)
REGISTER_SYSTEM(TTaggerB)
TEST(TypedSystem, UnorderedCommandsAddingTheSameTypeDoNotCollide) {
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
    // Not ordered against each other: only their commits are.
    FrameGraph graph; graph.phase({"custom"});
    graph.task("a", TTaggerA{}.describe(), []{}); graph.task("b", TTaggerB{}.describe(), []{});
    graph.compile();
    EXPECT_FALSE(graph.dependsOn("a", "b")); EXPECT_FALSE(graph.dependsOn("b", "a"));
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

// Commands are applied in the task's commit, in call order (even when one command
// is an argument of another); before it nothing is visible, not even to the task
// itself. Adding through the Scene inside a task needs write access.
TEST(TypedSystem, CommandsApplyInTheCommitInCallOrder) {
    registerTypedComponents();
    SceneInstance instance; auto& scene = instance.scene();
    const auto existing = scene.createEntity(); scene.addComponent<TPosition>(existing, 5);
    TypedAccess::Transaction tx;
    TaskDescription d; d.structuralChanges = true;
    Entity::ID created = Entity::invalidID, nestedChild = Entity::invalidID, nestedTagged = Entity::invalidID;
    {
        TypedAccess::TaskScope scope(scene, &tx, &d);
        auto commands = Commands::makeParam(scene);
        created = commands.create("created", existing).with(TPosition{ 1 }).with(TPosition{ 2 }).id();
        commands.entity(existing).with(TPosition{ 9 });                     // has one: keeps 5
        commands.entity(existing).remove<TPosition>().with(TPosition{ 7 }); // removed, then added
        commands.entity(created).with(TTag{});                              // created by the call above
        nestedChild = commands.create("child", commands.create("parent").id()).id(); // the parent is called first
        nestedTagged = commands.entity(commands.create("tagged").id()).with(TTag{}).id();
#if CSYREN_ACCESS_VALIDATION
        EXPECT_THROW(scene.addComponent<TVelocity>(existing), std::logic_error);
#endif
    }
    EXPECT_FALSE(scene.entities().contains(created)); // nothing before the commit
    EXPECT_EQ(scene.getComponent<const TPosition>(existing)->x, 5);
    tx.apply(scene);
    ASSERT_TRUE(scene.entities().contains(created));
    EXPECT_EQ(scene.entities().try_get(created)->name, "created");
    EXPECT_EQ(scene.entities().try_get(created)->parent, existing);
    EXPECT_EQ(scene.getComponent<const TPosition>(created)->x, 2); // the last with() wins
    EXPECT_TRUE(scene.hasComponent<TTag>(created));
    EXPECT_EQ(scene.getComponent<const TPosition>(existing)->x, 7);
    const auto parent = scene.entities().try_get(nestedChild)->parent;
    ASSERT_TRUE(scene.entities().contains(parent));
    EXPECT_EQ(scene.entities().try_get(parent)->name, "parent");
    EXPECT_TRUE(scene.hasComponent<TTag>(nestedTagged));
}

TEST(TypedSystem, CommandsForGoneEntitiesAndDestroy) {
    registerTypedComponents();
    SceneInstance instance; auto& scene = instance.scene();
    const auto parent = scene.createEntity("parent");
    const auto child = scene.createEntity("child", parent);
    const auto gone = scene.createEntity("gone");
    scene.destroyEntity(gone); scene.flush();
    {
        auto commands = Commands::makeParam(scene); // outside a task: applied when it goes away
        commands.entity(gone).with(TPosition{ 1 }).remove<TVelocity>().destroy();
        commands.entity(parent).with(TVelocity{ 3 }).destroy();
        EXPECT_TRUE(scene.entities().contains(parent)); // not yet
    }
    EXPECT_FALSE(scene.entities().contains(gone));
    EXPECT_FALSE(scene.entities().contains(parent)); // flushed after the commands
    EXPECT_FALSE(scene.entities().contains(child));  // with its children
}

// A frame that fails before the task's commit gives the reserved IDs back when
// the transaction is discarded.
TEST(TypedSystem, DiscardedCommandsGiveTheirIdsBack) {
    registerTypedComponents();
    SceneInstance instance; auto& scene = instance.scene();
    TypedAccess::Transaction tx;
    TaskDescription d; d.structuralChanges = true;
    Entity::ID id = Entity::invalidID;
    {
        TypedAccess::TaskScope scope(scene, &tx, &d);
        auto commands = Commands::makeParam(scene);
        id = commands.create("never").with(TPosition{ 1 }).id();
    }
    tx.discard(); // the commit never ran
    const auto next = scene.createEntity();
    EXPECT_EQ(Entity::slot(next), Entity::slot(id));
    EXPECT_NE(next, id);
    EXPECT_FALSE(scene.entities().contains(id));
}

// A create left by an exception gives its ID back and creates nothing.
TEST(TypedSystem, CreateLeftByAnExceptionCreatesNothing) {
    registerTypedComponents();
    SceneInstance instance; auto& scene = instance.scene();
    auto commands = Commands::makeParam(scene);
    Entity::ID id = Entity::invalidID;
    EXPECT_THROW({
        auto&& command = commands.create("never"); // kept until the end of the block
        command.with(TPosition{ 1 });
        id = command.id();
        throw std::runtime_error("execute failed");
    }, std::runtime_error);
    EXPECT_FALSE(scene.entities().contains(id));
    const auto next = scene.createEntity();
    EXPECT_EQ(Entity::slot(next), Entity::slot(id));
    EXPECT_NE(next, id);
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

// Without skips entities that have a type; single() is the only matching row.
class TWithoutReader : public TypedSystem<TWithoutReader> {
public:
    inline static int untagged = 0, moving = 0, only = -1, none = -1, several = -1;
    inline static bool containsTagged = true, containsUntagged = false;
    inline static Entity::ID tagged = Entity::invalidID, still = Entity::invalidID;
    void execute(Query<const TPosition>::Without<TTag> positions,
                 Query<const TPosition>::With<TVelocity>::Without<TTag> movers,
                 Query<const TVelocity>::With<TTag> taggedMovers) {
        untagged = 0; moving = 0;
        for (auto [id, position] : positions) untagged += position.x;
        for (auto [id, position] : movers) moving += position.x;
        containsTagged = positions.contains(tagged);
        containsUntagged = positions.contains(still);
        const auto row = movers.single();
        only = row ? std::get<1>(*row).x : -1;
        const auto severalRows = positions.single();
        several = severalRows ? std::get<1>(*severalRows).x : -1;
        const auto noRow = taggedMovers.single();
        none = noRow ? std::get<1>(*noRow).x : -1;
    }
};
TEST(TypedSystem, WithoutSkipsEntitiesAndSingleNeedsExactlyOne) {
    const auto d = TWithoutReader{}.describe();
    EXPECT_FALSE(findAccess(d, TypeKey::of<TTag>(), true)); // a filter, not an access
    EXPECT_TRUE(listed(d.filters, TypeKey::of<TTag>()));
    EXPECT_FALSE(d.structuralChanges);

    registerTypedComponents();
    ServiceScene services;
    details::ServiceRegistry::initializeAll();
    auto& scene = services.scene;
    TWithoutReader::tagged = scene.createEntity();
    scene.addComponent<TPosition>(TWithoutReader::tagged, 1); scene.addComponent<TTag>(TWithoutReader::tagged);
    TWithoutReader::still = scene.createEntity();
    scene.addComponent<TPosition>(TWithoutReader::still, 10);
    const auto mover = scene.createEntity();
    scene.addComponent<TPosition>(mover, 100); scene.addComponent<TVelocity>(mover, 1);
    TWithoutReader reader;
    reader.run();
    EXPECT_EQ(TWithoutReader::untagged, 110);
    EXPECT_EQ(TWithoutReader::moving, 100);
    EXPECT_FALSE(TWithoutReader::containsTagged);
    EXPECT_TRUE(TWithoutReader::containsUntagged);
    EXPECT_EQ(TWithoutReader::only, 100);
    EXPECT_EQ(TWithoutReader::several, -1); // two untagged entities
    EXPECT_EQ(TWithoutReader::none, -1);    // no tagged mover
}

TEST(TypedSystem, WithoutFiltersDoNotConflictWithWriters) {
    FrameGraph graph; graph.phase({"custom"});
    auto reader = TWithoutReader{}.describe(); reader.phase = "custom";
    TaskDescription writer; writer.phase = "custom"; writer.write<TTag>();
    graph.task("reader", reader, []{}); graph.task("writer", writer, []{});
    graph.compile();
    EXPECT_FALSE(graph.dependsOn("reader", "writer")); EXPECT_FALSE(graph.dependsOn("writer", "reader"));
}

// Components staged by the running task count for Without (as for With).
TEST(TypedSystem, WithoutSeesComponentsStagedByTheTask) {
    registerTypedComponents();
    SceneInstance instance; auto& scene = instance.scene();
    const auto a = scene.createEntity(); scene.addComponent<TPosition>(a, 1);
    const auto b = scene.createEntity(); scene.addComponent<TPosition>(b, 2);
    TypedAccess::Transaction tx;
    TaskDescription d; d.read<TPosition>().write<TTag>(); d.structuralChanges = true;
    TypedAccess::TaskScope scope(scene, &tx, &d);
    scene.addComponent<TTag>(a); // staged until the commit
    auto untagged = details::Param<Query<const TPosition>::Without<TTag>>::make(&scene);
    int sum = 0;
    for (auto [id, position] : untagged) sum += position.x;
    EXPECT_EQ(sum, 2);
    EXPECT_FALSE(untagged.contains(a));
}

// Queries follow an EntityRef kept in a component; it is empty once its entity is gone.
class TRefReader : public TypedSystem<TRefReader> {
public:
    inline static EntityRef target;
    inline static int seen = 0;
    inline static bool contained = false;
    void execute(Query<const TPosition> positions) {
        const auto position = positions.get<TPosition>(target);
        seen = position ? position->x : -1;
        contained = positions.contains(target);
    }
};
TEST(TypedSystem, QueriesFollowEntityRefs) {
    registerTypedComponents();
    ServiceScene services;
    details::ServiceRegistry::initializeAll();
    const auto id = services.scene.createEntity();
    services.scene.addComponent<TPosition>(id, 7);
    TRefReader::target = EntityRef{ id };
    TRefReader reader;
    reader.run();
    EXPECT_EQ(TRefReader::seen, 7);
    EXPECT_TRUE(TRefReader::contained);
    services.scene.destroyEntity(id);
    services.scene.flush();
    services.scene.addComponent<TPosition>(services.scene.createEntity(), 9); // takes the slot
    reader.run();
    EXPECT_EQ(TRefReader::seen, -1);
    EXPECT_FALSE(TRefReader::contained);
}
}
