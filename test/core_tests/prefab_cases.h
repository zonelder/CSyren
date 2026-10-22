#pragma once
// Prefab files (source/runtime/prefab_file.h) and the editor's prefab operations.
// Included by scene_workflow_tests.cpp: uses Link, CHECK, MemoryStorage and run().
#include "prefab_file.h"
#include "core/prefab.h"
#include "core/project_paths.h"
#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>

// A temporary project folder registered as ProjectPaths (prefab paths are asset paths).
struct PrefabProject
{
    std::filesystem::path root;
    PrefabProject()
    {
        static std::atomic<int> next{};
        root = std::filesystem::temp_directory_path() / ("CSyren-prefab-" + std::to_string(++next) + "-" +
            std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        std::filesystem::create_directories(root / "assets");
        core::details::ServiceRegistry::create<core::ProjectPaths>(core::ProjectPaths{ root, root / "assets" });
    }
    ~PrefabProject() { std::error_code ignored; std::filesystem::remove_all(root, ignored); }
};

// A component with a prefab reference: the library loads referenced prefabs too.
struct PrefabHolder
{
    core::PrefabRef next;
    static void describe()
    {
        core::reflection::MetaFactory<PrefabHolder>{}.type("PrefabHolder"_hs).data<&PrefabHolder::next>("next"_hs);
    }
};
static void registerPrefabTestTypes()
{
    static const bool registered = [] {
        core::PrefabRef::describe();
        REGISTER_COMPONENT(PrefabHolder);
        return true;
    }();
    (void)registered;
}

static core::Entity::ID prefabEntityNamed(core::Scene& scene, const std::string& name)
{
    for (const auto& entity : scene.entities()) if (entity.name == name) return entity.id;
    return core::Entity::invalidID;
}

static void prefabFileRoundtrip()
{
    PrefabProject project;
    SceneInstance source;
    auto& scene = source.scene();
    const auto outside = scene.createEntity("outside");
    const auto root = scene.createEntity("crate");
    const auto lid = scene.createEntity("lid", root);
    scene.addComponent<Transform>(root)->position = math::Vector3{ 5, 0, 0 };
    scene.addComponent<Transform>(lid)->position = math::Vector3{ 0, 1, 0 };
    scene.addComponent<Link>(root, Link{ lid });
    scene.addComponent<Link>(lid, Link{ root });
    std::string error;
    CHECK(savePrefabFile(scene, root, "assets/prefabs/crate.prefab", {}, error)); // "assets/" is the same asset path
    CHECK(std::filesystem::is_regular_file(project.root / "assets" / "prefabs" / "crate.prefab"));

    core::PrefabLibrary library;
    library.setLoader(prefabFileLoader({}));
    const auto prefab = library.load("prefabs/crate.prefab");
    CHECK(prefab);
    CHECK(prefab->path() == "prefabs/crate.prefab");
    SceneInstance target;
    core::SpawnPose pose;
    pose.position = math::Vector3{ 1, 2, 3 };
    const auto first = prefab->instantiate(target.scene(), pose);
    pose.position = math::Vector3{ 4, 5, 6 };
    const auto second = prefab->instantiate(target.scene(), pose);
    CHECK(first != core::Entity::invalidID && second != core::Entity::invalidID && first != second);
    for (const auto& [copy, x] : { std::pair{ first, 1.0f }, std::pair{ second, 4.0f } })
    {
        const auto* entity = target.scene().entities().try_get(copy);
        CHECK(entity && entity->name == "crate" && entity->children.size() == 1);
        const auto child = entity->children.front();
        CHECK(target.scene().entities().try_get(child)->name == "lid");
        CHECK(target.scene().getComponent<Transform>(copy)->position.x == x);    // the pose
        CHECK(target.scene().getComponent<Transform>(child)->position.y == 1.0f); // children keep theirs
        CHECK(target.scene().getComponent<Link>(copy)->target == child);         // references inside the copy
        CHECK(target.scene().getComponent<Link>(child)->target == copy);
    }
    // The file is an ordinary scene document: it can be imported as a scene.
    SceneInstance imported;
    std::ifstream file(project.root / "assets" / "prefabs" / "crate.prefab");
    const auto document = nlohmann::json::parse(file);
    JsonArchiveReader reader(document); SerializationContext context;
    CHECK(Serializer{}.loadScene(reader, context, imported.scene()));
    CHECK(prefabEntityNamed(imported.scene(), "lid") != core::Entity::invalidID);

    // A reference from the branch to an entity outside it cannot be saved.
    scene.getComponent<Link>(lid)->target = outside;
    CHECK(!savePrefabFile(scene, root, "prefabs/broken.prefab", {}, error));
    CHECK(error.find("lid") != std::string::npos);
}

static void prefabDocumentRules()
{
    BranchArchive archive;
    std::string error;
    std::vector<std::string> skipped;
    CHECK(!archive.fromDocument(nlohmann::json::parse(R"({"$version":2,"entities":[{"id":0,"name":"a"},{"id":1,"name":"b"}]})"), error));
    CHECK(error.find("one root") != std::string::npos);
    CHECK(!archive.fromDocument(nlohmann::json::parse(
        R"({"entities":[{"id":0,"name":"root"},{"id":1,"parent":2,"name":"a"},{"id":2,"parent":1,"name":"b"}]})"), error));
    CHECK(!archive.fromDocument(nlohmann::json::parse(R"({"entities":[{"id":0,"name":"a"},{"id":0,"parent":0,"name":"b"}]})"), error));
    CHECK(archive.fromDocument(nlohmann::json::parse(
        R"({"entities":[{"id":3,"parent":7,"name":"child"},{"id":7,"name":"root","components":{
            "Transform":{"position":[1,2,3],"rotation":[0,0,0,1],"scale":[1,1,1]},"NoSuchComponent":{}}}],
            "resources":[]})"), error, &skipped));
    CHECK(skipped == std::vector<std::string>{ "NoSuchComponent" });
    CHECK(archive.entities.size() == 2 && archive.entities[0].name == "root" && archive.entities[1].parent == 0);
    CHECK(archive.entities[0].components.size() == 1);
    CHECK(archive.sections.contains("resources") && !archive.sections.contains("entities"));
    CHECK(normalizePrefabPath("assets\\prefabs\\a.prefab") == "prefabs/a.prefab");
    CHECK(normalizePrefabPath("./prefabs/a.prefab") == "prefabs/a.prefab");
}

static void prefabReferencesAreLoadedTogether()
{
    registerPrefabTestTypes();
    PrefabProject project;
    SceneInstance source;
    auto& scene = source.scene();
    const auto leaf = scene.createEntity("leaf");
    scene.addComponent<Transform>(leaf);
    const auto holder = scene.createEntity("holder");
    scene.addComponent<PrefabHolder>(holder)->next.path = "assets/prefabs/leaf.prefab";
    const auto loop = scene.createEntity("loop");
    scene.addComponent<PrefabHolder>(loop)->next.path = "prefabs/loop.prefab"; // refers to itself
    std::string error;
    CHECK(savePrefabFile(scene, leaf, "prefabs/leaf.prefab", {}, error));
    CHECK(savePrefabFile(scene, holder, "prefabs/holder.prefab", {}, error));
    CHECK(savePrefabFile(scene, loop, "prefabs/loop.prefab", {}, error));
    core::PrefabLibrary library;
    std::vector<std::string> loaded;
    library.setLoader([&](const std::string& path, std::string& message) -> core::PrefabHandle {
        loaded.push_back(path); return loadPrefabFile(path, {}, message);
    });
    CHECK(library.load("prefabs/holder.prefab"));
    CHECK((loaded == std::vector<std::string>{ "prefabs/holder.prefab", "prefabs/leaf.prefab" }));
    CHECK(library.load("assets\\prefabs\\leaf.prefab")); // the same asset: cached
    CHECK(loaded.size() == 2);
    CHECK(library.load("prefabs/loop.prefab"));            // a cycle stops at the cache
    CHECK(loaded.size() == 3);
}

static void prefabMissingAndBrokenFiles()
{
    PrefabProject project;
    std::string error;
    CHECK(!loadPrefabFile("prefabs/none.prefab", {}, error));
    CHECK(error.find("not found") != std::string::npos);
    std::filesystem::create_directories(project.root / "assets" / "prefabs");
    std::ofstream(project.root / "assets" / "prefabs" / "bad.prefab") << "{ not json";
    CHECK(!loadPrefabFile("prefabs/bad.prefab", {}, error));
    // The library reports a failing path once and does not retry it.
    core::PrefabLibrary library;
    int loads = 0;
    library.setLoader([&](const std::string& path, std::string& message) -> core::PrefabHandle {
        ++loads; return loadPrefabFile(path, {}, message);
    });
    CHECK(!library.load("prefabs/bad.prefab"));
    CHECK(!library.load("prefabs/bad.prefab"));
    CHECK(loads == 1);
}

#ifdef _WIN32 // SceneSession tests need the Windows host library
static void prefabEditorWorkflow()
{
    PrefabProject project;
    core::details::ServiceRegistry::create<core::PrefabLibrary>().setLoader(prefabFileLoader({}));
    MemoryStorage storage;
    SceneInstance seed;
    const auto crate = seed.scene().createEntity("crate");
    seed.scene().addComponent<Transform>(crate);
    seed.scene().addComponent<Transform>(seed.scene().createEntity("lid", crate));
    CHECK(storage.write("prefab-scene", seed.scene()));
    SceneSession session(storage);
    run(session, SceneOperation::Open, "prefab-scene");
    const auto entities = [&] { size_t count = 0; for (const auto& entity : session.editorScene().entities()) { (void)entity; ++count; } return count; };
    const auto before = entities();

    SceneRequest save{ SceneOperation::SavePrefab, session.documentId(), "prefabs/crate.prefab" };
    save.entity = session.keyFor(prefabEntityNamed(session.editorScene(), "crate"));
    session.enqueue(save); session.processRequests(); CHECK(session.error().empty());
    CHECK(!session.dirty()); // saving a prefab does not change the scene

    SceneRequest place{ SceneOperation::InstantiatePrefab, session.documentId(), "prefabs/crate.prefab" };
    place.position = math::Vector3{ 0, 0, 9 };
    session.enqueue(place); session.processRequests(); CHECK(session.error().empty());
    const auto copy = session.keyFor(session.takeCreatedEntity());
    CHECK(session.editorScene().getComponent<Transform>(session.resolve(copy))->position.z == 9.0f);
    CHECK(entities() == before + 2);
    run(session, SceneOperation::Undo);
    CHECK(entities() == before);
    run(session, SceneOperation::Redo);
    CHECK(entities() == before + 2);
    CHECK(session.editorScene().entities().try_get(session.resolve(copy))->children.size() == 1);

    SceneRequest missing{ SceneOperation::InstantiatePrefab, session.documentId(), "prefabs/none.prefab" };
    session.enqueue(missing); session.processRequests();
    CHECK(!session.error().empty());
    CHECK(entities() == before + 2);
}
#endif
