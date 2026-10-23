#include "pch.h"
#include "core/spawn.h"
#include "core/frame_graph.h"
#include "core/system_manager.h"
#include "core/transform.h"
#include <array>
#include <map>

namespace {
using namespace csyren;
using namespace csyren::core;

struct SPayload { int value = 0; };
struct SOrigin { int count = 1; };
struct SMarker { int frame = 0; };
struct SSeen { int spawned = 0; };

void registerSpawnComponents() {
    REGISTER_COMPONENT(Transform);
    REGISTER_COMPONENT(SPayload); REGISTER_COMPONENT(SOrigin); REGISTER_COMPONENT(SMarker);
}

// A prefab built in code: a root with SPayload{7} and `children` children with a Transform.
class FakePrefab final : public Prefab {
public:
    explicit FakePrefab(std::string path, int children = 1) : _path(std::move(path)), _children(children) {}
    const std::string& path() const noexcept override { return _path; }
    Entity::ID instantiate(Scene& scene, const SpawnPose& pose, const std::vector<PrefabEdit>& edits,
        Entity::ID reservedRoot = Entity::invalidID) const override {
        ++instances;
        auto batch = scene.stageEntities(size_t(1 + _children), reservedRoot);
        const auto root = batch.createEntity("prefab");
        batch.addComponent<SPayload>(root)->value = 7;
        for (int i = 0; i < _children; ++i) {
            const auto child = batch.createEntity("child");
            batch.setParent(child, root);
            batch.addComponent<Transform>(child);
        }
        return finishInstance(scene, std::move(batch), root, pose, edits, *this);
    }
    mutable int instances = 0;
private:
    std::string _path;
    int _children;
};

// Files of the fake loader; counts how often the library had to load.
struct FakeFiles {
    std::map<std::string, std::shared_ptr<FakePrefab>> prefabs;
    int loads = 0;
};

struct SpawnServices {
    EventBus& bus = details::ServiceRegistry::create<EventBus>();
    Scene& scene = details::ServiceRegistry::create<Scene>(&bus);
    PrefabLibrary& prefabs = details::ServiceRegistry::create<PrefabLibrary>();
    FakeFiles library;
    SpawnServices() {
        library.prefabs["prefabs/box.prefab"] = std::make_shared<FakePrefab>("prefabs/box.prefab");
        prefabs.setLoader([this](const std::string& path, std::string& error) -> PrefabHandle {
            ++library.loads;
            auto it = library.prefabs.find(path);
            if (it == library.prefabs.end()) { error = "no such file"; return nullptr; }
            return it->second;
        });
        details::ServiceRegistry::initializeAll();
    }
    ~SpawnServices() { details::ServiceRegistry::shutdownAll(); }
    FakePrefab& box() { return *library.prefabs.at("prefabs/box.prefab"); }
};

size_t countPayloads(Scene& scene) {
    size_t count = 0;
    for (auto [id, payload] : scene.view<const SPayload>()) { (void)payload; ++count; }
    return count;
}

// Spawns one box per SOrigin, offset by the origin's count, tagging it with the frame.
class SSpawner : public TypedSystem<SSpawner> {
public:
    static constexpr auto phase = "custom";
    void execute(Select<SOrigin> origins, Spawn spawn) {
        ++_frame;
        for (auto [id, origin] : origins)
            spawn(PrefabRef{ "prefabs/box.prefab" }, { math::Vector3(float(origin.count), 0, 0) })
                .edit<SPayload>([count = origin.count](SPayload& payload) { payload.value += count; })
                .with(SMarker{ _frame });
    }
private:
    int _frame = 0;
};
REGISTER_SYSTEM(SSpawner)

// Ordered after the spawner: sees its copies in the same frame.
class SCounter : public TypedSystem<SCounter> {
public:
    static constexpr auto phase = "custom";
    static constexpr auto after = "SSpawner";
    void execute(Select<SMarker> markers, Res<SSeen> seen) {
        seen->spawned = 0;
        for (auto [id, marker] : markers) { (void)marker; ++seen->spawned; }
    }
};
REGISTER_SYSTEM(SCounter)

// A second, independent spawner of the same phase.
class SOtherSpawner : public TypedSystem<SOtherSpawner> {
public:
    static constexpr auto phase = "custom";
    void execute(Select<SOrigin> origins, Spawn spawn) {
        for (auto [id, origin] : origins) { (void)origin; spawn(PrefabRef{ "prefabs/box.prefab" }); }
    }
};
REGISTER_SYSTEM(SOtherSpawner)

// Insert and Spawn in the same task: entity ids must not collide.
class SMixed : public TypedSystem<SMixed> {
public:
    static constexpr auto phase = "custom";
    void execute(Insert<SPayload> insert, Spawn spawn) {
        for (int i = 0; i < 3; ++i) {
            insert.add<SPayload>(insert.create("inserted"), 100 + i);
            spawn(PrefabRef{ "prefabs/box.prefab" });
        }
    }
};
REGISTER_SYSTEM(SMixed)

// Keeps the roots of its copies: known when spawning, alive after the commit.
class SRootSpawner : public TypedSystem<SRootSpawner> {
public:
    static constexpr auto phase = "custom";
    inline static std::vector<Entity::ID> roots;
    void execute(Select<SOrigin> origins, Spawn spawn) {
        for (auto [id, origin] : origins) { (void)origin; roots.push_back(spawn(PrefabRef{ "prefabs/box.prefab" }).root()); }
        roots.push_back(spawn(PrefabRef{ "prefabs/missing.prefab" }).root());
        roots.push_back(spawn(PrefabRef{}).root());
    }
};
REGISTER_SYSTEM(SRootSpawner)
}

TEST(Spawn, DeclaresOnlyAStructuralChange) {
    const auto d = SSpawner{}.describe();
    EXPECT_TRUE(d.structuralChanges);
    EXPECT_FALSE(d.exclusiveScene);
    EXPECT_FALSE(d.dynamicResources);
    EXPECT_TRUE(d.inserts.empty());
    for (const auto& access : d.accesses)
        EXPECT_TRUE(!access.component || access.type == TypeKey::of<SOrigin>()) << access.type.name;
}

TEST(Spawn, SpawnersDoNotConflictButTheirCommitsAreOrdered) {
    FrameGraph graph; graph.phase({ "custom" });
    TaskDescription writer; writer.phase = "custom"; writer.write<SPayload>().write<SMarker>();
    graph.task("a", SSpawner{}.describe(), [] {});
    graph.task("b", SOtherSpawner{}.describe(), [] {});
    graph.task("writer", writer, [] {});
    graph.compile();
    EXPECT_FALSE(graph.dependsOn("a", "b")); EXPECT_FALSE(graph.dependsOn("b", "a"));
    // Readers and writers of the prefab's components are not ordered against a spawner.
    EXPECT_FALSE(graph.dependsOn("writer", "a")); EXPECT_FALSE(graph.dependsOn("a", "writer"));
}

TEST(Spawn, CommitsOfSpawnersAreOrderedButTheSpawnersAreNot) {
    registerSpawnComponents();
    SpawnServices services;
    SystemManager systems; systems.phase({ "custom" });
    systems.jobs().init(2);
    systems.add("SSpawner"); systems.add("SOtherSpawner"); systems.init();
    systems.execute();
    const auto& graph = systems.executionGraph();
    const auto index = [&](const std::string& name) {
        for (size_t i = 0; i < graph.tasks.size(); ++i) if (graph.tasks[i].task.name == name) return i;
        ADD_FAILURE() << "no task " << name; return size_t(0);
    };
    // parents are indices of the topologically ordered list: walk them for reachability.
    const auto reaches = [&](size_t child, size_t parent) {
        std::vector<size_t> pending{ child };
        std::vector<bool> seen(graph.tasks.size());
        while (!pending.empty()) {
            const auto current = pending.back(); pending.pop_back();
            for (auto p : graph.tasks[current].parents) {
                if (p == parent) return true;
                if (!seen[p]) { seen[p] = true; pending.push_back(p); }
            }
        }
        return false;
    };
    const auto a = index("SSpawner"), b = index("SOtherSpawner");
    const auto applyA = index("SSpawner/apply"), applyB = index("SOtherSpawner/apply");
    EXPECT_FALSE(reaches(a, b)); EXPECT_FALSE(reaches(b, a));
    EXPECT_TRUE(reaches(applyA, applyB) || reaches(applyB, applyA)); // exclusive commits
    EXPECT_TRUE(reaches(applyA, a)); EXPECT_TRUE(reaches(applyB, b));
    systems.shutdown();
}

TEST(Spawn, CopiesAppearInTheSpawnersCommit) {
    registerSpawnComponents();
    SpawnServices services;
    auto& seen = details::ServiceRegistry::create<SSeen>();
    services.scene.addComponent<SOrigin>(services.scene.createEntity(), 2);
    services.scene.addComponent<SOrigin>(services.scene.createEntity(), 5);
    SystemManager systems; systems.phase({ "custom" });
    systems.jobs().init(4);
    systems.add("SCounter"); systems.add("SSpawner"); // order decided by `after` and the commit
    systems.init();
    systems.execute();
    EXPECT_EQ(seen.spawned, 2); // same frame, after SSpawner/apply
    EXPECT_EQ(countPayloads(services.scene), 2u);
    int sum = 0;
    for (auto [id, payload, transform] : services.scene.view<const SPayload, const Transform>()) {
        sum += payload.value;                                   // 7 + count (edit)
        EXPECT_FLOAT_EQ(transform.position.x, float(payload.value - 7)); // pose
        EXPECT_EQ(services.scene.getComponent<const SMarker>(id)->frame, 1); // with
        EXPECT_EQ(services.scene.entities().try_get(id)->children.size(), 1u); // the whole branch
    }
    EXPECT_EQ(sum, 7 + 2 + 7 + 5);
    EXPECT_EQ(services.library.loads, 1); // cached by the library
    systems.execute();
    EXPECT_EQ(seen.spawned, 4);
    systems.shutdown();
}

TEST(Spawn, InsertAndSpawnInOneTaskGetDistinctEntities) {
    registerSpawnComponents();
    SpawnServices services;
    SystemManager systems; systems.phase({ "custom" });
    systems.jobs().init(2);
    systems.add("SMixed"); systems.init();
    EXPECT_NO_THROW(systems.execute());
    EXPECT_NO_THROW(systems.execute());
    // 2 frames x (3 inserted + 3 copies with a child each).
    EXPECT_EQ(countPayloads(services.scene), 12u);
    size_t entities = 0;
    for (const auto& entity : services.scene.entities()) { (void)entity; ++entities; }
    EXPECT_EQ(entities, 1u + 6u + 12u); // root + inserted + copies (root and child)
    EXPECT_EQ(services.box().instances, 6);
    systems.shutdown();
}

TEST(Spawn, OutsideTheGraphChainedEditsStillApply) {
    registerSpawnComponents();
    SpawnServices services;
    services.scene.addComponent<SOrigin>(services.scene.createEntity(), 3);
    SSpawner spawner;
    spawner.run();
    ASSERT_EQ(countPayloads(services.scene), 1u);
    for (auto [id, payload] : services.scene.view<const SPayload>()) {
        EXPECT_EQ(payload.value, 10);
        EXPECT_TRUE(services.scene.hasComponent<SMarker>(id));
    }
}

TEST(Spawn, PoseScaleAndParent) {
    registerSpawnComponents();
    SpawnServices services;
    const auto parent = services.scene.createEntity("parent");
    services.scene.addComponent<Transform>(parent);
    SpawnPose pose;
    pose.position = math::Vector3(1, 2, 3);
    pose.scale = math::Vector3(2, 2, 2);
    pose.parent = parent;
    const Prefab& box = services.box();
    const auto root = box.instantiate(services.scene, pose);
    ASSERT_NE(root, Entity::invalidID);
    EXPECT_EQ(services.scene.entities().try_get(root)->parent, parent);
    auto transform = services.scene.getComponent<const Transform>(root);
    ASSERT_TRUE(transform);
    EXPECT_FLOAT_EQ(transform->position.z, 3.0f);
    EXPECT_FLOAT_EQ(transform->scale.y, 2.0f);
}

TEST(Spawn, MissingPrefabsAndEmptyReferencesSpawnNothing) {
    registerSpawnComponents();
    SpawnServices services;
    const auto spawnAll = [&](Spawn spawn) {
        spawn(PrefabRef{});                                     // not set
        spawn(PrefabRef{ "prefabs/missing.prefab" });           // library returns null
        spawn(PrefabHandle{});                                   // null handle
        spawn(services.library.prefabs.at("prefabs/box.prefab")) // edit of a component the root lacks
            .edit<SMarker>([](SMarker& marker) { marker.frame = 99; });
    };
    spawnAll(Spawn::makeParam(services.scene));
    EXPECT_EQ(countPayloads(services.scene), 1u); // only the box, without the failed edit
    for (auto [id, payload] : services.scene.view<const SPayload>()) {
        (void)payload;
        EXPECT_FALSE(services.scene.hasComponent<SMarker>(id));
    }
    EXPECT_EQ(services.library.loads, 1); // the empty reference never reaches the library
    spawnAll(Spawn::makeParam(services.scene));
    EXPECT_EQ(services.library.loads, 1); // the missing path is remembered, not retried
    EXPECT_EQ(countPayloads(services.scene), 2u);
}

TEST(Spawn, LibraryCachesAndInvalidates) {
    PrefabLibrary library;
    int loads = 0;
    library.setLoader([&](const std::string& path, std::string&) -> PrefabHandle { ++loads; return std::make_shared<FakePrefab>(path); });
    const auto a = library.load("prefabs/a.prefab");
    EXPECT_EQ(library.load("prefabs/a.prefab"), a);
    EXPECT_EQ(loads, 1);
    library.invalidate("prefabs/a.prefab");
    EXPECT_NE(library.load("prefabs/a.prefab"), nullptr);
    EXPECT_EQ(loads, 2);
    PrefabLibrary empty;
    EXPECT_EQ(empty.load("prefabs/a.prefab"), nullptr);
}

TEST(Spawn, RootIsKnownWhenSpawning) {
    registerSpawnComponents();
    SpawnServices services;
    services.scene.addComponent<SOrigin>(services.scene.createEntity(), 1);
    SRootSpawner::roots.clear();
    SystemManager systems; systems.phase({ "custom" });
    systems.jobs().init(2);
    systems.add("SRootSpawner"); systems.init();
    systems.execute();
    ASSERT_EQ(SRootSpawner::roots.size(), 3u);
    const auto copy = SRootSpawner::roots[0], missing = SRootSpawner::roots[1], none = SRootSpawner::roots[2];
    ASSERT_TRUE(services.scene.entities().contains(copy));
    EXPECT_EQ(services.scene.getComponent<const SPayload>(copy)->value, 7);
    EXPECT_EQ(services.scene.entities().try_get(copy)->children.size(), 1u);
    EXPECT_EQ(none, Entity::invalidID);
    // The copy of a missing prefab was not created: its ID never names an entity.
    ASSERT_NE(missing, Entity::invalidID);
    EXPECT_FALSE(services.scene.entities().contains(missing));
    const auto next = services.scene.createEntity();
    EXPECT_EQ(Entity::slot(next), Entity::slot(missing)); // the slot came back...
    EXPECT_NE(next, missing);                             // ...with the next generation
    systems.shutdown();
}

TEST(Spawn, RequestLeftByAnExceptionGivesItsRootBack) {
    registerSpawnComponents();
    SpawnServices services;
    auto spawn = Spawn::makeParam(services.scene);
    Entity::ID root = Entity::invalidID;
    EXPECT_THROW({
        auto&& request = spawn(PrefabRef{ "prefabs/box.prefab" });
        root = request.root();
        throw std::runtime_error("execute failed");
    }, std::runtime_error);
    EXPECT_EQ(countPayloads(services.scene), 0u);
    const auto next = services.scene.createEntity();
    EXPECT_EQ(Entity::slot(next), Entity::slot(root));
    EXPECT_NE(next, root);
}
