#include "pch.h"
#include "core/frame_graph.h"
#include "core/scene.h"
#include "core/gpu_execution_plan.h"
#include "core/system_manager.h"
#include "core/scene_context.h"
#include <atomic>
#include <thread>
#include <chrono>

namespace csyren::core::details {
struct SceneJobTestAccess {
    using Transaction = Scene::Transaction;
    using TaskScope = Scene::TaskScope;
};
}
namespace {
using namespace csyren::core;
using SceneJobAccess = details::SceneJobTestAccess;
struct Position { int x = 0; };
struct Velocity { int x = 0; };
struct Resource {};
void registerJobComponents() {
    REGISTER_COMPONENT(Position); REGISTER_COMPONENT(Velocity);
}
TaskDescription task(const char* phase = "custom") { TaskDescription d; d.phase = phase; return d; }

TEST(FrameGraph, PhaseClosureIncludesEmptyPhasesAndExplicitOrderWins) {
    FrameGraph graph;
    graph.phase({"input"}); graph.phase({"empty", {"input"}}); graph.phase({"custom", {"empty"}});
    auto writer = task(); writer.write<Position>(); writer.after = {"reader"};
    auto reader = task(); reader.read<Position>();
    graph.task("writer", writer, []{});
    graph.task("reader", reader, []{});
    graph.task("input", task("input"), []{});
    graph.compile();
    EXPECT_TRUE(graph.dependsOn("writer", "reader"));
    EXPECT_TRUE(graph.dependsOn("reader", "input"));
    EXPECT_FALSE(graph.dependsOn("reader", "writer"));
}
TEST(FrameGraph, ReadReadAndDisjointWritesStayIndependent) {
    FrameGraph graph; graph.phase({"custom"});
    auto a = task(); a.read<Position>().write<Velocity>();
    auto b = task(); b.read<Position>();
    graph.task("a", a, []{}); graph.task("b", b, []{}); graph.compile();
    EXPECT_FALSE(graph.dependsOn("a", "b")); EXPECT_FALSE(graph.dependsOn("b", "a"));
}
TEST(FrameGraph, RejectsBadPhasesSystemsAndCycles) {
    FrameGraph graph; graph.phase({"custom", {"missing"}});
    EXPECT_THROW(graph.compile(), std::invalid_argument);
    graph.clear(); graph.phase({"a", {"b"}}); graph.phase({"b", {"a"}});
    EXPECT_THROW(graph.compile(), std::invalid_argument);
    graph.clear(); graph.phase({"custom"}); auto a = task(); a.after = {"missing"};
    graph.task("a", a, []{}); EXPECT_THROW(graph.compile(), std::invalid_argument);
    graph.clear(); graph.phase({"custom"}); a.after = {"b"}; auto b = task(); b.after = {"a"};
    graph.task("a", a, []{}); graph.task("b", b, []{}); EXPECT_THROW(graph.compile(), std::invalid_argument);
}
TEST(FrameGraph, CPUResourceStatesAreChecked) {
    FrameGraph graph; graph.phase({"custom"}); graph.resourceState<Resource>("empty");
    auto producer = task(); producer.writeResource<Resource>("empty", "ready");
    auto consumer = task(); consumer.readResource<Resource>("ready");
    graph.task("producer", producer, []{}); graph.task("consumer", consumer, []{}); graph.compile();
    EXPECT_TRUE(graph.dependsOn("consumer", "producer"));
    graph.clear(); graph.phase({"custom"}); graph.task("bad", consumer, []{});
    EXPECT_THROW(graph.compile(), std::invalid_argument);
}
TEST(FrameGraph, RunsIndependentWorkAndPinsUIAndDrainsExceptions) {
    csyren::cstdmf::multicore::JobSystem jobs; jobs.init(3);
    FrameGraph graph; graph.phase({"custom"});
    std::atomic<int> entered{0}; std::atomic<bool> overlap{true};
    auto work = [&] {
        entered.fetch_add(1);
        auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
        while (entered.load() < 2) {
            if (std::chrono::steady_clock::now() > deadline) { overlap = false; break; }
            std::this_thread::yield();
        }
    };
    graph.task("a", task(), work); graph.task("b", task(), work);
    auto ui = task(); ui.after = {"a", "b"}; ui.affinity = TaskAffinity::MainThread;
    auto main = std::this_thread::get_id(); bool pinned = false;
    graph.task("ui", ui, [&] { pinned = std::this_thread::get_id() == main; });
    graph.execute(jobs); EXPECT_TRUE(overlap); EXPECT_TRUE(pinned);
    graph.clear(); graph.phase({"custom"});
    graph.task("throws", task(), [] { throw std::runtime_error("expected"); });
    auto next = task(); next.after = {"throws"}; bool called = false;
    graph.task("next", next, [&] { called = true; });
    EXPECT_THROW(graph.execute(jobs), std::runtime_error); EXPECT_FALSE(called);
    graph.clear(); graph.phase({"custom"}); graph.task("recovered", task(), [&] { called = true; });
    EXPECT_NO_THROW(graph.execute(jobs)); EXPECT_TRUE(called); jobs.fini();
}
TEST(SceneJobs, TransparentCreateReadModifyAndCommit) {
    registerJobComponents();
    auto bus = std::make_unique<EventBus2>(); Scene scene(bus.get()); scene.init();
    SceneJobAccess::Transaction transaction;
    ComponentRef<Position> reference; Entity::ID id;
    auto d = task(); d.write<Position>(); d.structuralChanges = true;
    {
        SceneJobAccess::TaskScope scope(scene, &transaction, &d);
        id = scene.createEntity("created in job");
        reference = scene.addComponent<Position>(id);
        reference->x = 42;
        EXPECT_TRUE(scene.hasComponent<Position>(id));
        EXPECT_EQ(scene.getComponent<const Position>(id)->x, 42);
        EXPECT_TRUE(scene.view<const Position>().contains(id)); // Read your own staged writes.
        scene.flush(); // Cannot accidentally invalidate an active view.
    }
    EXPECT_FALSE(scene.hasComponent<Position>(id));
    transaction.apply(scene);
    ASSERT_TRUE(reference); EXPECT_EQ(reference->x, 42);
    EXPECT_EQ(reference, scene.getComponent<Position>(id));
    EXPECT_EQ(scene.getComponent<const Position>(id)->x, 42);
    scene.removeComponent<Position>(id); scene.flush(); scene.addComponent<Position>(id, 99);
    EXPECT_FALSE(reference);
}
TEST(SceneJobs, UndeclaredWritesAndStructuralChangesFailBeforeMutation) {
    auto bus = std::make_unique<EventBus2>(); Scene scene(bus.get()); scene.init();
    auto d = task(); d.read<Position>();
    SceneJobAccess::TaskScope scope(scene, nullptr, &d);
    EXPECT_NO_THROW(scene.view<const Position>());
    EXPECT_THROW(scene.view<Position>(), std::logic_error);
    EXPECT_THROW(scene.createEntity(), std::logic_error);
}
TEST(GpuPlan, CrossQueueReadersEachWaitForProducerAndWriterWaitsForAllReaders) {
    constexpr uint32_t initial = 0, uav = 8, srv = 16;
    auto description = [&](const char* queue, uint32_t state, AccessMode mode) {
        auto d = task(); d.gpuQueue = queue; d.gpu("image", state, mode); return d;
    };
    GpuExecutionPlan plan;
    plan.compile({
        {"produce", description("a", uav, AccessMode::Write)},
        {"read1", description("b", srv, AccessMode::Read)},
        {"read2", description("c", srv, AccessMode::Read)},
        {"read3", description("d", srv, AccessMode::Read)},
        {"write", description("e", uav, AccessMode::Write)}
    }, {{"image", initial, initial}}, uav);
    const auto& passes = plan.passes();
    ASSERT_EQ(passes.size(), 5u);
    EXPECT_EQ(passes[1].waits, std::vector<size_t>{0});
    EXPECT_EQ(passes[2].waits, std::vector<size_t>{1});
    EXPECT_EQ(passes[3].waits, std::vector<size_t>{1});
    EXPECT_EQ(passes[4].waits, (std::vector<size_t>{1,2,3}));
    EXPECT_EQ(passes[0].barriers[0].before, initial);
    EXPECT_EQ(passes[0].barriers[0].after, uav);
    EXPECT_EQ(plan.finalBarriers()[0].after, initial);
}
TEST(GpuPlan, SameStateReadsWaitForOriginalWriterAndUAVWritesGetBarrier) {
    auto usage = [](const char* queue, AccessMode mode) {
        auto d = task(); d.gpuQueue = queue; d.gpu("image", 8, mode); return d;
    };
    GpuExecutionPlan plan;
    plan.compile({{"write", usage("a", AccessMode::Write)},
        {"read1", usage("b", AccessMode::Read)}, {"read2", usage("c", AccessMode::Read)},
        {"write2", usage("a", AccessMode::Write)}}, {{"image",8,8}}, 8);
    EXPECT_EQ(plan.passes()[1].waits, std::vector<size_t>{0});
    EXPECT_EQ(plan.passes()[2].waits, std::vector<size_t>{0});
    EXPECT_TRUE(plan.passes()[1].barriers[0].uav);
    EXPECT_TRUE(plan.passes()[3].barriers[0].uav);
    EXPECT_TRUE(plan.finalBarriers().empty());
}

TEST(FrameGraph, StateProducerIsResolvedBeforeRegistrationOrder) {
    FrameGraph graph; graph.phase({"custom"});
    auto reader = task(); reader.readResource<Resource>("ready");
    auto producer = task(); producer.writeResource<Resource>({}, "ready");
    graph.task("reader", reader, []{}); graph.task("producer", producer, []{});
    graph.compile(); EXPECT_TRUE(graph.dependsOn("reader", "producer"));
}
TEST(FrameGraph, DisjointStructuralTasksRunBeforeTheirCommits) {
    FrameGraph graph; graph.phase({"custom"});
    auto a = task(); a.write<Position>(); a.structuralChanges = true;
    auto b = task(); b.write<Velocity>(); b.structuralChanges = true;
    auto applyA = task(); applyA.after = {"a"}; applyA.exclusiveScene = true; applyA.structuralCommitFor = "a";
    auto applyB = task(); applyB.after = {"b"}; applyB.exclusiveScene = true; applyB.structuralCommitFor = "b";
    graph.task("a", a, []{}); graph.task("a/apply", applyA, []{});
    graph.task("b", b, []{}); graph.task("b/apply", applyB, []{});
    graph.compile();
    EXPECT_TRUE(graph.dependsOn("a/apply", "b")); EXPECT_TRUE(graph.dependsOn("b/apply", "a"));
    EXPECT_FALSE(graph.dependsOn("a", "b")); EXPECT_FALSE(graph.dependsOn("b", "a"));
}
class StructuralProducer : public System {
public:
    TaskDescription describe() const override { auto d = task(); d.write<Position>(); d.structuralChanges = true; return d; }
    void execute() override {
        auto* scene = Services::get<Scene>();
        auto id = scene->createEntity(); scene->addComponent<Position>(id)->x = 77;
    }
};
class StructuralConsumer : public System {
public:
    static inline int observed = 0;
    TaskDescription describe() const override { auto d = task(); d.read<Position>(); d.after = {"StructuralProducer"}; return d; }
    void execute() override {
        for (auto [id, position] : Services::get<Scene>()->view<const Position>()) observed = position.x;
    }
};
REGISTER_SYSTEM(StructuralProducer)
REGISTER_SYSTEM(StructuralConsumer)
TEST(SystemManager, DependencyIncludesInvisibleStructuralCommit) {
    registerJobComponents();
    auto& bus = details::ServiceRegistry::create<EventBus2>();
    details::ServiceRegistry::create<Scene>(&bus);
    details::ServiceRegistry::initializeAll();
    SystemManager systems; systems.phase({"custom"});
    systems.add("StructuralConsumer"); systems.add("StructuralProducer"); systems.init();
    StructuralConsumer::observed = 0;
    systems.execute(); EXPECT_EQ(StructuralConsumer::observed, 77);
    systems.shutdown(); details::ServiceRegistry::shutdownAll();
}
TEST(ServicesJobs, ReadsRequireConstPointersAndUndeclaredResourcesAreRejected) {
    auto& resource = details::ServiceRegistry::create<Resource>();
    auto d = task(); d.readResource<Resource>();
    {
        Services::TaskScope scope(d);
        EXPECT_EQ(Services::get<const Resource>(), &resource);
        EXPECT_THROW(Services::get<Resource>(), std::logic_error);
        EXPECT_THROW(Services::get<Velocity>(), std::logic_error);
    }
    details::ServiceRegistry::shutdownAll();
}

TEST(SceneJobs, DiscardInvalidatesEscapedPendingReferences) {
    auto bus = std::make_unique<EventBus2>(); Scene scene(bus.get()); scene.init();
    SceneJobAccess::Transaction transaction;
    ComponentRef<Position> pending;
    {
        SceneJobAccess::TaskScope scope(scene, &transaction);
        auto id = scene.createEntity(); pending = scene.addComponent<Position>(id);
        EXPECT_TRUE(pending);
    }
    transaction.discard(); EXPECT_FALSE(pending);
}

TEST(SceneJobs, ParallelCreatorsReserveUniqueIDsAndQueryTheirOwnComponents) {
    registerJobComponents();
    auto bus = std::make_unique<EventBus2>(); Scene scene(bus.get()); scene.init();
    SceneJobAccess::Transaction ta, tb;
    auto a = task(); a.write<Position>(); a.structuralChanges = true;
    auto b = task(); b.write<Velocity>(); b.structuralChanges = true;
    auto commit = [&](const char* owner) {
        auto d = task(); d.after = {owner}; d.exclusiveScene = true;
        d.structuralCommitFor = owner; d.affinity = TaskAffinity::MainThread; return d;
    };
    std::vector<Entity::ID> idsA, idsB;
    int ownA = 0, ownB = 0;
    std::atomic<int> entered{0}; std::atomic<bool> overlapped{true};
    auto overlap = [&] {
        entered.fetch_add(1);
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
        while (entered.load() != 2) {
            if (std::chrono::steady_clock::now() > deadline) { overlapped = false; break; }
            std::this_thread::yield();
        }
    };
    FrameGraph graph; graph.phase({"custom"});
    graph.task("a", a, [&] {
        SceneJobAccess::TaskScope scope(scene, &ta, &a); overlap();
        for (int i = 0; i < 500; ++i) {
            auto id = scene.createEntity(); idsA.push_back(id); scene.addComponent<Position>(id)->x = i;
        }
        for (auto [id, value] : scene.view<const Position>()) ++ownA;
    });
    graph.task("a/apply", commit("a"), [&] { ta.apply(scene); });
    graph.task("b", b, [&] {
        SceneJobAccess::TaskScope scope(scene, &tb, &b); overlap();
        for (int i = 0; i < 500; ++i) {
            auto id = scene.createEntity(); idsB.push_back(id); scene.addComponent<Velocity>(id)->x = i;
        }
        for (auto [id, value] : scene.view<const Velocity>()) ++ownB;
    });
    graph.task("b/apply", commit("b"), [&] { tb.apply(scene); });
    csyren::cstdmf::multicore::JobSystem jobs; jobs.init(3); graph.execute(jobs); jobs.fini();
    EXPECT_TRUE(overlapped); EXPECT_EQ(ownA, 500); EXPECT_EQ(ownB, 500);
    idsA.insert(idsA.end(), idsB.begin(), idsB.end()); std::sort(idsA.begin(), idsA.end());
    EXPECT_EQ(std::unique(idsA.begin(), idsA.end()) - idsA.begin(), 1000);
    EXPECT_EQ(scene.entities().size(), 1001u);
}

class BoundSceneProbe : public System {
public:
    inline static std::vector<Scene*> observed;
    TaskDescription describe() const override { auto d = task(); d.affinity = TaskAffinity::MainThread; d.readResource<EventBus2>(); return d; }
    void execute() override {
        if (Services::get<Scene>() != &scene() || Services::get<const EventBus2>() != &sceneBus())
            throw std::logic_error("Graph lost the scene binding");
        observed.push_back(&scene());
    }
};
TEST(SystemManager, GraphFollowsEditorAndRuntimeBindings) {
    details::SystemRegistry::instance().registerSystem("BoundShared", [] { return std::make_shared<BoundSceneProbe>(); });
    details::SystemRegistry::instance().registerSystem("BoundEditor", [] { return std::make_shared<BoundSceneProbe>(); });
    SceneInstance editor, runtime;
    SystemManager systems; systems.phase({"custom"});
    systems.add("BoundShared"); systems.addEditor("BoundShared", EditorSystemScene::Active);
    systems.addEditor("BoundEditor", EditorSystemScene::Editor);
    systems.bind(editor.context(), editor.context(), false); systems.init();
    auto check = [&](Scene* active) {
        BoundSceneProbe::observed.clear(); systems.execute();
        EXPECT_EQ(BoundSceneProbe::observed, (std::vector<Scene*>{active, &editor.scene()}));
    };
    check(&editor.scene());
    systems.bind(editor.context(), runtime.context(), true); check(&runtime.scene());
    systems.bind(editor.context(), editor.context(), false); check(&editor.scene());
    systems.shutdown();
}

}
