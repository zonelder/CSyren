#include "gtest/gtest.h"
#include "editor_layout.h"
#include "scene_gizmo.h"
#include "scene_session.h"
#include "serializer.h"
#include "cstdmf/file_system.h"
#include "meta_common.h"
#include "core/meta_json.h"
#include "core/scene_serialization_context.h"
#include "core/system_manager.h"
#include "core/time.h"
#include "core/transform.h"
#include "physics/physic_system.h"
#include "physics/colliders.h"
#include "physics/rigid_body.h"
#include "editor_base.h"
#include "inspector_window.h"
#include "editor_selection.h"
#include "scene_workflow_window.h"
#include "editor_viewport_state.h"
#include "editor_camera_controller_system.h"
#include "project_settings.h"
#include "startup_options.h"
#include "render_environment_system.h"
#include "scene_picking.h"
#include "dx12_graphic/resource_handle_meta.h"
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>

#define CHECK(condition) do { if (!(condition)) { ADD_FAILURE() << "Check failed: " #condition; throw std::runtime_error("Check failed: " #condition); } } while (false)

using namespace csyren;
using namespace csyren::core;
using namespace csyren::editor;

struct Link
{
    Entity::ID target{};
    static void describe()
    {
        using namespace core::reflection;
        MetaFactory<Link>{}.primitiveType("WorkflowLink"_hs).archive({
            [](ArchiveWriter& writer, SerializationContext& context, const MetaType&, const void* value) {
                auto* refs = context.tryGet<SceneEntityReferences>();
                if (!refs) return false;
                auto it = refs->saved.find(static_cast<const Link*>(value)->target);
                return it != refs->saved.end() && writer.unsignedInteger(it->second);
            },
            [](ArchiveReader& reader, SerializationContext& context, const MetaType&, void* value) {
                auto* refs = context.tryGet<SceneEntityReferences>(); uint64_t id{};
                if (!refs || !reader.unsignedInteger(id) || !refs->loaded.contains(id)) return false;
                static_cast<Link*>(value)->target = refs->loaded.at(id); return true;
            }
        });
    }
};

class MemoryStorage final : public SceneStorage
{
public:
    Serializer serializer;
    std::unordered_map<std::string, std::string> files;
    bool failWrite{}, failSnapshot{}, failRestore{};
    size_t snapshots{}, restores{}, failSnapshotAt{}, failRestoreAt{};
    bool snapshot(Scene& scene, std::string& archive) override
    {
        if (++snapshots == failSnapshotAt || failSnapshot) return false;
        nlohmann::json document; JsonArchiveWriter writer(document); SerializationContext context;
        if (!serializer.saveScene(writer, context, scene)) return false;
        archive = document.dump(); return true;
    }
    bool restore(const std::string& archive, Scene& scene) override
    {
        if (++restores == failRestoreAt || failRestore) return false;
        auto document = nlohmann::json::parse(archive, nullptr, false);
        if (document.is_discarded()) return false;
        JsonArchiveReader reader(document); SerializationContext context;
        return serializer.loadScene(reader, context, scene);
    }
    bool read(const std::string& path, Scene& scene) override
    { return files.contains(path) && restore(files.at(path), scene); }
    bool write(const std::string& path, Scene& scene) override
    {
        std::string archive;
        if (failWrite || !snapshot(scene, archive)) return false;
        files[path] = std::move(archive); return true;
    }
};

void run(SceneSession& session, SceneOperation operation, std::string path = {}, bool discard = false)
{
    session.enqueue(operation, std::move(path), discard); session.processRequests();
    CHECK(session.error().empty());
}
void position(SceneSession& session, EntityKey key, math::Vector3 value, bool finished = true)
{
    auto transform = session.editorScene().getComponent<Transform>(session.resolve(key));
    CHECK(transform);
    auto previous = transform->position; transform->position = value;
    session.preview(key, reflection::ComponentFamily::getID<Transform>(), { "position"_hs.id }, previous, value, finished);
}
EntityKey lastObject(SceneSession& session)
{
    Entity::ID result = Entity::invalidID;
    for (const auto& entity : session.editorScene().entities()) if (entity.name == "New object") result = entity.id;
    CHECK(result != Entity::invalidID); return session.keyFor(result);
}

void documentWorkflow()
{
    MemoryStorage storage;
    SceneInstance source;
    auto entity = source.scene().createEntity("source");
    source.scene().addComponent<Transform>(entity);
    CHECK(storage.write("source", source.scene()));
    SceneSession session(storage);
    run(session, SceneOperation::Open, "source");
    CHECK(session.editorScene().entities().size() == 2);
    CHECK(!session.dirty());
    run(session, SceneOperation::CreateEntity);
    const auto key = lastObject(session);
    const auto id = session.resolve(key);
    position(session, key, math::Vector3{ 1, 2, 3 }, false);
    position(session, key, math::Vector3{ 4, 5, 6 }, false);
    CHECK(session.dirty());
    run(session, SceneOperation::Save); // ends the whole drag as one command
    CHECK(!session.dirty());
    position(session, key, math::Vector3{ 4, 5, 6 });
    CHECK(!session.dirty()); // no-op must not create a state token
    position(session, key, math::Vector3{ 4.000001f, 5, 6 });
    CHECK(session.dirty()); // below math's epsilon is still a real authoring edit
    run(session, SceneOperation::Undo); CHECK(!session.dirty());
    const auto editor = session.editorContext().instance;
    run(session, SceneOperation::Play);
    CHECK(session.playing()); CHECK(session.activeContext().instance != editor);
    CHECK(session.editorContext().instance == editor);
    for (auto [runtimeId, transform] : session.activeContext().scene.view<Transform>())
        transform.position = math::Vector3{ 99, 99, 99 };
    auto projectile = session.activeContext().scene.createEntity("runtime-only");
    (void)projectile;
    session.enqueue(SceneOperation::SaveAs, "forbidden"); session.processRequests();
    CHECK(!session.error().empty()); CHECK(!storage.files.contains("forbidden"));
    run(session, SceneOperation::Stop);
    CHECK(!session.playing()); CHECK(!session.dirty());
    CHECK(session.editorContext().instance == editor);
    CHECK(session.editorScene().getComponent<Transform>(id)->position == math::Vector3(4, 5, 6));
    run(session, SceneOperation::Undo);
    CHECK(session.editorScene().getComponent<Transform>(id)->position == math::Vector3(0, 0, 0));
    run(session, SceneOperation::Undo);
    CHECK(session.resolve(key) == Entity::invalidID);
    // Force a different ECS ID on Redo without changing the command's logical target.
    auto filler = session.editorScene().createEntity("allocator filler");
    (void)filler;
    run(session, SceneOperation::Redo);
    CHECK(session.resolve(key) != id);
    run(session, SceneOperation::Redo);
    CHECK(session.editorScene().getComponent<Transform>(session.resolve(key))->position == math::Vector3(4, 5, 6));
    CHECK(!session.dirty());
    run(session, SceneOperation::Undo);
    position(session, key, math::Vector3{ 7, 8, 9 });
    CHECK(session.dirty()); CHECK(!session.canRedo()); // same depth, different saved-state token
    position(session, key, math::Vector3{ 8, 9, 10 }, false);
    CHECK(session.cancelEdit());
    CHECK(session.editorScene().getComponent<Transform>(session.resolve(key))->position == math::Vector3(7, 8, 9));
    const auto originalFile = storage.files.at("source");
    storage.failWrite = true;
    session.enqueue(SceneOperation::SaveAs, "other"); session.processRequests();
    CHECK(!session.error().empty()); CHECK(session.dirty()); CHECK(session.path() == "source");
    CHECK(storage.files.at("source") == originalFile);
    storage.failWrite = false;
    // Dirty Replace is guarded; invalid archives do not destroy the document or history.
    storage.files["bad"] = "{broken";
    const auto beforeFailure = session.editorContext().instance;
    session.enqueue(SceneOperation::Open, "bad", true); session.processRequests();
    CHECK(!session.error().empty()); CHECK(session.editorContext().instance == beforeFailure); CHECK(session.canUndo());
    session.enqueue(SceneOperation::Reload); session.processRequests();
    CHECK(!session.error().empty()); CHECK(session.editorContext().instance == beforeFailure);
    run(session, SceneOperation::Reload, {}, true);
    CHECK(!session.canUndo()); CHECK(!session.dirty());
    auto size = session.editorScene().entities().size();
    run(session, SceneOperation::Reload); CHECK(session.editorScene().entities().size() == size);
    // Play must include unsaved authoring values and preserve dirty/history on Stop.
    run(session, SceneOperation::CreateEntity);
    const auto unsaved = lastObject(session);
    position(session, unsaved, math::Vector3{ 10, 20, 30 });
    run(session, SceneOperation::Play);
    bool copied = false;
    for (auto [runtimeId, transform] : session.activeContext().scene.view<Transform>())
        copied |= transform.position == math::Vector3(10, 20, 30);
    CHECK(copied);
    run(session, SceneOperation::Stop); CHECK(session.dirty()); CHECK(session.canUndo());
    storage.failSnapshot = true;
    session.enqueue(SceneOperation::Play); session.processRequests();
    CHECK(!session.error().empty()); CHECK(!session.playing());
    storage.failSnapshot = false;
    storage.failRestore = true;
    session.enqueue(SceneOperation::Play); session.processRequests();
    CHECK(!session.error().empty()); CHECK(!session.playing());
    storage.failRestore = false;
    // Requests queued for an old document cannot mutate a replacement.
    session.enqueue(SceneOperation::Open, "source", true);
    session.enqueue(SceneOperation::CreateEntity);
    session.processRequests();
    CHECK(!session.error().empty()); CHECK(session.editorScene().entities().size() == size);
}

void importWorkflow()
{
    MemoryStorage storage;
    SceneInstance source;
    auto parent = source.scene().createEntity("import parent");
    auto child = source.scene().createEntity("import child", parent);
    source.scene().addComponent<Transform>(child);
    source.scene().addComponent<Link>(parent)->target = child;
    CHECK(storage.write("import", source.scene()));
    SceneSession session(storage);
    run(session, SceneOperation::CreateEntity);
    const auto key = lastObject(session);
    position(session, key, math::Vector3{ 1, 2, 3 });
    run(session, SceneOperation::SaveAs, "document");
    run(session, SceneOperation::Import, "import");
    CHECK(session.path() == "document"); CHECK(session.dirty());
    CHECK(session.editorScene().entities().size() == 4);
    for (auto [id, link] : session.editorScene().view<Link>())
    {
        CHECK(session.editorScene().entities().try_get(link.target)->parent == id);
        CHECK(session.editorScene().hasComponent<Transform>(link.target));
    }
    run(session, SceneOperation::Undo);
    CHECK(session.editorScene().entities().size() == 2); CHECK(!session.dirty());
    run(session, SceneOperation::Undo); // earlier field command survives scene restoration
    CHECK(session.editorScene().getComponent<Transform>(session.resolve(key))->position == math::Vector3(0, 0, 0));
    run(session, SceneOperation::Redo);
    run(session, SceneOperation::Redo);
    CHECK(session.editorScene().entities().size() == 4);
    run(session, SceneOperation::Play); run(session, SceneOperation::Stop);
    run(session, SceneOperation::Undo); CHECK(!session.dirty());
    storage.files["bad import"] = "{broken";
    const auto instance = session.editorContext().instance;
    session.enqueue(SceneOperation::Import, "bad import"); session.processRequests();
    CHECK(!session.error().empty()); CHECK(session.editorContext().instance == instance); CHECK(!session.dirty());
}

class Probe : public System
{
public:
    int attachedCount{}, detachedCount{}, updatedCount{}, alive{}, initCount{}, shutdownCount{};
    bool fail{}, failInit{};
    Scene* target{};
    void init() override { ++initCount; if (failInit) throw std::runtime_error("Injected system init failure"); }
    void shutdown() override { ++shutdownCount; }
    void update() override { ++updatedCount; }
protected:
    void onAttach() override
    {
        ++attachedCount; ++alive; target = &scene();
        if (fail) throw std::runtime_error("Injected runtime attach failure");
    }
    void onDetach() noexcept override { ++detachedCount; --alive; target = nullptr; }
};

void systemLifecycle()
{
    MemoryStorage storage; SceneSession session(storage); SystemManager manager;
    auto editor = std::make_shared<Probe>();
    auto presentation = std::make_shared<Probe>();
    auto runtime = std::make_shared<Probe>();
    core::details::SystemRegistry::instance().registerSystem("WorkflowEditor", [editor] { return editor; });
    core::details::SystemRegistry::instance().registerSystem("WorkflowPresentation", [presentation] { return presentation; });
    core::details::SystemRegistry::instance().registerSystem("WorkflowRuntime", [runtime] { return runtime; });
    session.setBind([&](auto document, auto active, bool playing) { manager.bind(document, active, playing); });
    manager.add("WorkflowPresentation"); manager.add("WorkflowRuntime");
    manager.addEditor("WorkflowPresentation", EditorSystemScene::Active);
    manager.addEditor("WorkflowEditor");
    manager.init(); manager.update();
    CHECK(presentation->initCount == 1); // one instance shared by both schedules
    CHECK(manager.list().size() == 2);
    CHECK(manager.list()[0]->name == "WorkflowPresentation");
    CHECK(manager.list()[1]->name == "WorkflowEditor");
    CHECK(editor->updatedCount == 1); CHECK(presentation->updatedCount == 1); CHECK(runtime->updatedCount == 0);
    runtime->fail = true;
    session.enqueue(SceneOperation::Play); session.processRequests();
    CHECK(!session.error().empty()); CHECK(!session.playing()); CHECK(runtime->alive == 0);
    CHECK(presentation->target == &session.editorScene()); CHECK(editor->attachedCount == 1);
    runtime->fail = false;
    for (int i = 0; i < 4; ++i)
    {
        run(session, SceneOperation::Play); manager.update();
        CHECK(manager.list().size() == 3);
        CHECK(manager.list()[0]->name == "WorkflowPresentation");
        CHECK(manager.list()[1]->name == "WorkflowRuntime");
        CHECK(manager.list()[2]->name == "WorkflowEditor");
        CHECK(runtime->alive == 1); CHECK(runtime->target != &session.editorScene());
        run(session, SceneOperation::Stop); CHECK(runtime->alive == 0);
        CHECK(presentation->target == &session.editorScene());
    }
    CHECK(editor->attachedCount == 1); CHECK(runtime->updatedCount == 4);
    manager.unbind(); manager.shutdown(); CHECK(editor->alive == 0); CHECK(presentation->alive == 0);
    CHECK(presentation->shutdownCount == 1);
}

void systemsConfiguration()
{
    auto editor = std::make_shared<Probe>(), shared = std::make_shared<Probe>(), game = std::make_shared<Probe>();
    int sharedCreated = 0;
    auto& registry = core::details::SystemRegistry::instance();
    registry.registerSystem("ConfigEditor", [editor] { return editor; });
    registry.registerSystem("ConfigShared", [shared, &sharedCreated] { ++sharedCreated; return shared; });
    registry.registerSystem("ConfigGame", [game] { return game; });
    const auto directory = std::filesystem::temp_directory_path();
    const auto prefix = L"CSyren-config-" + std::to_wstring(GetCurrentProcessId());
    const auto appFile = directory / (prefix + L".app.systems");
    const auto editorFile = directory / (prefix + L".editor.systems");
    struct Cleanup
    {
        std::filesystem::path app, editor;
        ~Cleanup() { std::error_code ignored; std::filesystem::remove(app, ignored); std::filesystem::remove(editor, ignored); }
    } cleanup{ appFile, editorFile };
    auto utf8 = [](const auto& file) { auto path = file.u8string(); return std::string(reinterpret_cast<const char*>(path.data()), path.size()); };
    {
        std::ofstream(appFile) << R"({"systems":[{"class":"ConfigGame"},{"class":"ConfigShared"}]})";
        std::ofstream(editorFile) << R"({"systems":[{"class":"ConfigShared","scene":"active"},{"class":"ConfigEditor"}]})";
    }
    MemoryStorage storage; SceneSession session(storage); SystemManager manager; Serializer serializer;
    session.setBind([&](auto document, auto active, bool playing) { manager.bind(document, active, playing); });
    CHECK(serializer.loadSystems(utf8(appFile), utf8(editorFile), manager));
    CHECK(sharedCreated == 1 && shared->initCount == 1);
    manager.update(); CHECK(game->updatedCount == 0 && editor->updatedCount == 1 && shared->updatedCount == 1);
    manager.move(0, 1); manager.update();
    CHECK(manager.configuration().editor[0].name == "ConfigEditor");
    run(session, SceneOperation::Play); manager.update();
    CHECK(manager.list()[0]->name == "ConfigGame" && manager.list()[1]->name == "ConfigShared");
    CHECK(shared->target == &session.activeContext().scene && editor->target == &session.editorScene());
    manager.move(0, 1); manager.update();
    CHECK(manager.configuration().app[0] == "ConfigShared");
    CHECK(serializer.saveSystems(utf8(appFile), utf8(editorFile), manager));
    run(session, SceneOperation::Stop);
    CHECK(serializer.loadSystems(utf8(appFile), utf8(editorFile), manager));
    CHECK(manager.configuration().app[0] == "ConfigShared");
    CHECK(manager.configuration().editor[0].name == "ConfigEditor");
    const int initialized = shared->initCount;
    std::ofstream(editorFile) << R"({"systems":[{"class":"ConfigShared","scene":"invalid"}]})";
    CHECK(!serializer.loadSystems(utf8(appFile), utf8(editorFile), manager));
    CHECK(shared->initCount == initialized && shared->attached());
    std::ofstream(editorFile) << R"({"systems":[{"class":"MissingSystem"}]})";
    CHECK(!serializer.loadSystems(utf8(appFile), utf8(editorFile), manager));
    CHECK(shared->initCount == initialized && shared->attached());
    manager.unbind(); manager.shutdown();
    // Registry factories outlive this test; do not retain its local counter.
    registry.registerSystem("ConfigShared", [shared] { return shared; });
}

void systemGraphPreviews()
{
    class PlanProbe final : public Probe {
    public:
        TaskDescription plan;
        TaskDescription describe() const override { return plan; }
    };
    class PlanBackend final : public TaskBackend {
    public:
        int compiled = 0;
        void describe(TaskDescription& d) const override { d.readResource<int>("ready"); }
        void compile(const std::vector<GraphTask>&) override { ++compiled; }
        void record(const std::string&, const std::function<void()>& invoke) override { invoke(); }
    };
    auto producer = std::make_shared<PlanProbe>(), consumer = std::make_shared<PlanProbe>();
    auto pinned = std::make_shared<PlanProbe>(), editorOnly = std::make_shared<PlanProbe>();
    for (const auto& probe : {producer, consumer, pinned, editorOnly}) probe->plan.phase = "simulation";
    producer->plan.structuralChanges = true;
    consumer->plan.after = {"PreviewProducer"};
    auto& registry = core::details::SystemRegistry::instance();
    registry.registerSystem("PreviewProducer", [producer] { return producer; });
    registry.registerSystem("PreviewConsumer", [consumer] { return consumer; });
    registry.registerSystem("PreviewPinned", [pinned] { return pinned; });
    registry.registerSystem("PreviewEditorOnly", [editorOnly] { return editorOnly; });
    SceneInstance editor, game;
    SystemManager manager;
    manager.phase({"simulation"}); manager.resourceState<int>("ready");
    auto backend = std::make_shared<PlanBackend>(); manager.taskBackend(backend);
    manager.add("PreviewProducer"); manager.add("PreviewConsumer");
    manager.addEditor("PreviewPinned");
    manager.addEditor("PreviewEditorOnly", EditorSystemScene::Active);
    const auto gamePlan = manager.previewExecutionGraph(true).graph;
    CHECK(manager.previewExecutionGraph(true).error.empty());
    CHECK(gamePlan.tasks.size() == 4); // producer + apply, consumer, pinned editor
    const auto standalonePlan = manager.previewExecutionGraph(true, true).graph;
    CHECK(manager.previewExecutionGraph(true, true).error.empty());
    CHECK(standalonePlan.tasks.size() == 3); // app systems only, no pinned editor
    for (const auto& node : standalonePlan.tasks)
        CHECK(node.task.name != "PreviewPinned" && node.task.name != "PreviewEditorOnly");
    CHECK(manager.previewExecutionGraph(false).graph.tasks.size() == 2);
    CHECK(producer->initCount == 0 && producer->attachedCount == 0 && producer->updatedCount == 0);
    CHECK(backend->compiled == 0 && manager.executionGraph().tasks.empty());
    auto compare = [](const auto& plan, const auto& actual) {
        CHECK(plan.tasks.size() == actual.tasks.size());
        for (size_t i = 0; i < plan.tasks.size(); ++i) {
            CHECK(plan.tasks[i].task.name == actual.tasks[i].task.name);
            CHECK(plan.tasks[i].parents == actual.tasks[i].parents);
            CHECK(plan.tasks[i].task.description.accesses.size() == actual.tasks[i].task.description.accesses.size());
        }
    };
    manager.bind(editor.context(), editor.context(), false); manager.init(); manager.execute();
    compare(manager.previewExecutionGraph(false).graph, manager.executionGraph());
    CHECK(producer->attachedCount == 0 && producer->updatedCount == 0);
    const int compiled = backend->compiled;
    manager.previewExecutionGraph(true);
    CHECK(backend->compiled == compiled && !manager.executionGraphPending() && !manager.playing());
    manager.bind(editor.context(), game.context(), true); manager.execute();
    compare(gamePlan, manager.executionGraph());
    CHECK(producer->updatedCount == 1 && pinned->updatedCount == 2 && editorOnly->updatedCount == 1);
    manager.bind(editor.context(), editor.context(), false); manager.execute();
    producer->plan.after = {"MissingPreviewTask"};
    manager.resourceState<int>("ready"); // invalidate cached plans
    CHECK(!manager.previewExecutionGraph(true).error.empty());
    CHECK(manager.previewExecutionGraph(false).error.empty());
    manager.execute(); // an invalid inactive plan must not affect the active schedule
    producer->plan.after.clear(); manager.resourceState<int>("ready");
    CHECK(manager.previewExecutionGraph(true).error.empty());
    manager.remove("PreviewEditorOnly");
    CHECK(manager.previewExecutionGraph(false).graph.tasks.size() == 1);
    manager.unbind(); manager.shutdown();
    // The standalone preview must match a host that executes only app.systems.
    auto& standaloneBus = core::details::ServiceRegistry::create<EventBus2>();
    auto& standaloneScene = core::details::ServiceRegistry::create<Scene>(&standaloneBus);
    standaloneScene.init();
    SystemManager standalone;
    standalone.phase({"simulation"}); standalone.resourceState<int>("ready");
    standalone.taskBackend(backend);
    standalone.add("PreviewProducer"); standalone.add("PreviewConsumer");
    standalone.init(); standalone.execute();
    compare(standalonePlan, standalone.executionGraph());
    standalone.shutdown(); core::details::ServiceRegistry::shutdownAll();
}

void physicsLifecycle()
{
    core::details::ServiceRegistry::create<physics::PhysicsEngine>();
    core::details::ServiceRegistry::create<Time>();
    MemoryStorage storage; SceneSession session(storage); SystemManager manager;
    auto entity = session.editorScene().createEntity("falling body");
    session.editorScene().addComponent<Transform>(entity)->position = math::Vector3{ 0, 10, 0 };
    session.editorScene().addComponent<physics::BoxCollider>(entity);
    session.editorScene().addComponent<physics::RigidBody>(entity)->type = physics::BodyType::Dynamic;
    session.settle();
    session.setBind([&](auto document, auto active, bool playing) { manager.bind(document, active, playing); });
    manager.add("PhysicsSystem"); manager.init();
    core::details::TimeHandler clock;
    for (int i = 0; i < 8; ++i)
    {
        run(session, SceneOperation::Play);
        clock.update(*Services::get<Time>()); manager.update();
        auto created = session.activeContext().scene.createEntity("temporary body");
        session.activeContext().scene.addComponent<Transform>(created);
        session.activeContext().scene.addComponent<physics::BoxCollider>(created);
        session.activeContext().scene.addComponent<physics::RigidBody>(created);
        session.settle();
        session.activeContext().scene.destroyEntity(created); session.settle();
        run(session, SceneOperation::Stop);
        CHECK(session.editorScene().getComponent<Transform>(entity)->position == math::Vector3(0, 10, 0));
    }
    manager.unbind(); manager.shutdown();
    core::details::ServiceRegistry::shutdownAll();
}

void fileStorage()
{
    auto directory = std::filesystem::temp_directory_path() / (L"CSyren-workflow-" + std::to_wstring(GetCurrentProcessId()));
    std::filesystem::create_directories(directory);
    auto file = directory / L"\u0441\u0446\u0435\u043d\u0430.scene";
    struct Cleanup
    {
        std::filesystem::path file, directory;
        ~Cleanup() { std::error_code ignored; std::filesystem::remove(file, ignored); std::filesystem::remove(directory, ignored); }
    } cleanup{ file, directory };
    const auto pathUtf8 = file.u8string();
    const std::string path(reinterpret_cast<const char*>(pathUtf8.data()), pathUtf8.size());
    Serializer serializer; SceneInstance scene;
    auto entity = scene.scene().createEntity("persisted"); scene.scene().addComponent<Transform>(entity);
    CHECK(serializer.saveScene(path, scene.scene()));
    std::ifstream input(file, std::ios::binary);
    const std::string original{ std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>() };
    input.close();
    scene.scene().getComponent<Transform>(entity)->position = math::Vector3{ 1, 2, 3 };
    const auto locked = CreateFileW(file.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    CHECK(locked != INVALID_HANDLE_VALUE);
    const bool saved = serializer.saveScene(path, scene.scene());
    CloseHandle(locked);
    CHECK(!saved);
    std::ifstream afterFailure(file, std::ios::binary);
    const std::string retained{ std::istreambuf_iterator<char>(afterFailure), std::istreambuf_iterator<char>() };
    afterFailure.close(); CHECK(retained == original);
    size_t files{}; for (const auto& item : std::filesystem::directory_iterator(directory)) { (void)item; ++files; }
    CHECK(files == 1); // failed rename also cleans the temporary file
    CHECK(serializer.saveScene(path, scene.scene()));
    SceneInstance loaded; CHECK(serializer.loadScene(path, loaded.scene()));
    CHECK(loaded.scene().entities().size() == 2);
    for (auto [id, transform] : loaded.scene().view<Transform>()) CHECK(transform.position == math::Vector3(1, 2, 3));
    auto& filesystem = cstdmf::FileSystem::instance();
    const auto mountedFile = directory / "mounted.scene";
    struct MountCleanup {
        std::filesystem::path file;
        ~MountCleanup() { cstdmf::FileSystem::instance().unmount("workflow"); std::error_code ignored; std::filesystem::remove(file, ignored); }
    } mountedCleanup{mountedFile};
    CHECK(filesystem.mount("workflow", directory, true));
    CHECK(serializer.saveScene("workflow:/mounted.scene", scene.scene()));
    SceneInstance mounted; CHECK(serializer.loadScene("workflow:/mounted.scene", mounted.scene()));
    CHECK(mounted.scene().entities().size() == 2);
    CHECK(filesystem.mount("workflow", directory, false));
    CHECK(!serializer.saveScene("workflow:/mounted.scene", scene.scene()));

}

void startupDiagnostics()
{
    const auto file = std::filesystem::temp_directory_path() / (L"CSyren-startup-" + std::to_wstring(GetCurrentProcessId()) + L"-\u043b\u043e\u0433.log");
    struct Cleanup
    {
        std::filesystem::path file;
        ~Cleanup() { csyren::log::shutdown(); std::error_code ignored; std::filesystem::remove(file, ignored); }
    } cleanup{ file };
    CHECK(csyren::log::init(file));
    csyren::log::info("Startup log opened before project initialization");
    CHECK(csyren::log::init(file)); // reinitialization must not poison the stream
    const auto missingProject = file.parent_path() / (file.stem().wstring() + L".csyrenproj");
    try { ProjectSettings::load(missingProject); CHECK(false); }
    catch (const std::exception& error) { csyren::log::exception(error); }
    try
    {
        try { throw std::runtime_error("inner diagnostic cause"); }
        catch (...) { std::throw_with_nested(std::runtime_error("outer diagnostic context")); }
    }
    catch (const std::exception& error) { csyren::log::exception("startup nested failure", error); }
    struct FailingStartupService
    {
        void init() { throw std::runtime_error("Injected service init failure"); }
    };
    core::details::ServiceRegistry::create<FailingStartupService>();
    bool failed = false;
    try { core::details::ServiceRegistry::initializeAll(); }
    catch (const std::exception&) { failed = true; }
    CHECK(failed); core::details::ServiceRegistry::shutdownAll();
    auto system = std::make_shared<Probe>(); system->failInit = true;
    core::details::SystemRegistry::instance().registerSystem("FailingStartupSystem", [system] { return system; });
    SystemManager manager; manager.add("FailingStartupSystem");
    failed = false;
    try { manager.init(); } catch (const std::exception&) { failed = true; }
    CHECK(failed); manager.shutdown();
    // Errors must be visible in the file before shutdown or process teardown.
    std::ifstream input(file);
    const std::string contents{ std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>() };
    CHECK(contents.find("Startup log opened") != std::string::npos);
    CHECK(contents.find("Exception [") != std::string::npos);
    CHECK(contents.find("Cannot open project: " + core::pathToUtf8(missingProject)) != std::string::npos);
    CHECK(contents.find("outer diagnostic context") != std::string::npos);
    CHECK(contents.find("inner diagnostic cause") != std::string::npos);
    CHECK(contents.find("Injected service init failure") != std::string::npos);
    CHECK(contents.find("FailingStartupService") != std::string::npos);
    CHECK(contents.find("system 'FailingStartupSystem' init") != std::string::npos);
    CHECK(contents.find("Injected system init failure") != std::string::npos);
}

void renderEnvironmentDefaults()
{
    render::Material material;
    render::Primitives::initializeMaterialDefaults(material);
    auto data = material.getVariable("tint");
    CHECK(data.size() == sizeof(DirectX::XMFLOAT4));
    DirectX::XMFLOAT4 tint;
    std::memcpy(&tint, data.data(), sizeof(tint));
    CHECK(tint.x == 1 && tint.y == 1 && tint.z == 1 && tint.w == 1);
    const auto version = material.version();
    render::Primitives::initializeMaterialDefaults(material);
    CHECK(material.version() == version);
    material.setVector("tint", DirectX::XMFLOAT4(0.2f, 0.4f, 0.6f, 1));
    render::Primitives::initializeMaterialDefaults(material);
    data = material.getVariable("tint"); std::memcpy(&tint, data.data(), sizeof(tint));
    CHECK(tint.x == 0.2f && tint.y == 0.4f && tint.z == 0.6f);
    const auto light = render::RenderEnvironmentSystem::lightDirection(0);
    CHECK(std::abs(light.x * light.x + light.y * light.y + light.z * light.z - 1) < 1e-5f);
    CHECK(light.y < 0 && light.w == 0);
    const auto repeat = render::RenderEnvironmentSystem::lightDirection(10);
    CHECK(std::abs(light.x - repeat.x) < 1e-5f && std::abs(light.y - repeat.y) < 1e-5f);
    // The old diffuse shader must produce a nonzero color on an upward face.
    CHECK(tint.x * -light.y > 0);
    core::details::ServiceRegistry::create<render::ResourceManager>();
    render::RenderEnvironmentSystem environment;
    environment.init(); // factory registration requires no startup scene or GPU
    core::details::ServiceRegistry::shutdownAll();
}

void editorCameraWorkerExecution()
{
    auto& devices = core::details::ServiceRegistry::create<core::Devices>();
    core::details::ServiceRegistry::create<core::Time>();
    auto& view = core::details::ServiceRegistry::create<EditorViewportState>();
    auto& mouse = const_cast<core::MouseDevice&>(devices.mouse());
    mouse.setButtonState(core::MouseButton::Right, true);
    mouse.setMousePosition(20, 10);
    const auto rotation = view.transform.rotation;
    EditorCameraControllerSystem controller;
    const auto description = controller.describe();
    CHECK(description.affinity == core::TaskAffinity::Worker);
    // The controller must not serialize unrelated ECS transform work.
    core::FrameGraph graph; graph.phase({"simulation"});
    graph.task("camera", description, []{});
    core::TaskDescription transforms; transforms.phase = "simulation"; transforms.write<Transform>();
    graph.task("transforms", transforms, []{}); graph.compile();
    CHECK(!graph.dependsOn("camera", "transforms") && !graph.dependsOn("transforms", "camera"));
    std::exception_ptr failure;
    std::thread worker([&] {
        try { core::Services::TaskScope scope(description); controller.execute(); }
        catch (...) { failure = std::current_exception(); }
    });
    worker.join();
    if (failure) std::rethrow_exception(failure);
    CHECK(view.transform.rotation.x != rotation.x || view.transform.rotation.y != rotation.y ||
        view.transform.rotation.z != rotation.z || view.transform.rotation.w != rotation.w);
    core::details::ServiceRegistry::shutdownAll();
}

void startupArguments()
{
    auto parse = [](std::initializer_list<std::wstring_view> values) {
        return StartupOptions::parse(std::span<const std::wstring_view>(values.begin(), values.size()));
    };
    CHECK(parse({}).project == "CSyren.csyrenproj");
    CHECK(parse({ L"--project", L"D:\\My Game\\game.csyrenproj" }).project == L"D:\\My Game\\game.csyrenproj");
    CHECK(parse({ L"--project=game.csyrenproj" }).project == L"game.csyrenproj");
    CHECK(parse({ L"--project", L"\u043f\u0440\u043e\u0435\u043a\u0442.csyrenproj" }).project == L"\u043f\u0440\u043e\u0435\u043a\u0442.csyrenproj");
    auto rejected = [&](std::initializer_list<std::wstring_view> values, const char* diagnostic) {
        try { parse(values); }
        catch (const std::invalid_argument& error) { CHECK(std::string(error.what()).find(diagnostic) != std::string::npos); return; }
        CHECK(false);
    };
    rejected({ L"/bigobj" }, "Unknown argument: /bigobj");
    rejected({ L"game.csyrenproj" }, "Unknown argument");
    rejected({ L"--unknown" }, "Unknown argument");
    rejected({ L"--project" }, "Missing value");
    rejected({ L"--project", L"--unknown" }, "Missing value");
    rejected({ L"--project=" }, "Empty value");
    rejected({ L"--project", L"" }, "Empty value");
    rejected({ L"--project=a", L"--project", L"b" }, "Duplicate --project");
    rejected({ L"--project=a", L"/bigobj" }, "Unknown argument");
}

void projectConfiguration()
{
    const auto cwd = std::filesystem::current_path();
    const auto directory = std::filesystem::temp_directory_path() / (L"CSyren-project-" + std::to_wstring(GetCurrentProcessId()));
    const auto firstRoot = directory / L"First", secondRoot = directory / L"\u0432\u0442\u043e\u0440\u043e\u0439";
    const auto firstFile = directory / L"First.csyrenproj", secondFile = directory / L"Second.csyrenproj";
    struct Cleanup
    {
        std::filesystem::path directory, firstRoot, secondRoot, firstFile, secondFile;
        ~Cleanup()
        {
            std::error_code ignored;
            for (const auto& root : { firstRoot, secondRoot }) {
                for (const auto& name : { "app.systems", "editor.systems", "start.scene", "saved.scene" })
                    std::filesystem::remove(root / "Content" / name, ignored);
                std::filesystem::remove(root / "Content", ignored); std::filesystem::remove(root, ignored);
            }
            std::filesystem::remove(firstFile, ignored); std::filesystem::remove(secondFile, ignored);
            std::filesystem::remove(directory, ignored);
        }
    } cleanup{ directory, firstRoot, secondRoot, firstFile, secondFile };
    std::filesystem::create_directories(firstRoot / "Content"); std::filesystem::create_directories(secondRoot / "Content");
    ProjectSettings first; first.file = firstFile; first.name = "First"; first.projectPath = "First";
    first.assetsPath = "Content"; first.appSystems = "Content/app.systems";
    first.editorSystems = "Content/editor.systems"; first.startScene = "Content/start.scene";
    ProjectSettings second = first; second.file = secondFile; second.name = "Second";
    second.projectPath = core::pathToUtf8(secondRoot.filename());
    auto game = std::make_shared<Probe>(), ui = std::make_shared<Probe>();
    auto& registry = core::details::SystemRegistry::instance();
    registry.registerSystem("ProjectGame", [game] { return game; });
    registry.registerSystem("ProjectEditor", [ui] { return ui; });
    Serializer serializer;
    for (const auto& settings : { first, second }) {
        const auto paths = settings.paths();
        std::ofstream(paths.resolve(core::pathFromUtf8(settings.appSystems))) << R"({"systems":[{"class":"ProjectGame"}]})";
        std::ofstream(paths.resolve(core::pathFromUtf8(settings.editorSystems))) << R"({"systems":[{"class":"ProjectEditor"}]})";
        SceneInstance scene; (void)scene.scene().createEntity(settings.name);
        std::string snapshot; CHECK(serializer.snapshot(scene.scene(), snapshot));
        std::ofstream(paths.resolve(core::pathFromUtf8(settings.startScene))) << snapshot;
        CHECK(settings.save());
    }
    for (const auto& file : { firstFile, secondFile }) {
        const auto settings = ProjectSettings::load(file);
        const auto paths = settings.paths();
        CHECK(paths.root == (file == firstFile ? firstRoot : secondRoot));
        CHECK(paths.assets == paths.root / "Content");
        core::details::ServiceRegistry::create<ProjectPaths>(paths);
        CHECK(core::projectAssetPath("start.scene") == paths.assets / "start.scene");
        CHECK(core::projectBuildPath("shader.hlsl") == paths.root / "build" / "shader.hlsl");
        CHECK(core::projectAssetPath(firstFile) == firstFile); // absolute paths are preserved
        ArchiveSceneStorage storage(serializer); SceneSession session(storage); SystemManager manager;
        session.setBind([&](auto document, auto active, bool playing) { manager.bind(document, active, playing); });
        CHECK(serializer.loadSystems(core::pathToUtf8(paths.resolve(core::pathFromUtf8(settings.appSystems))),
            core::pathToUtf8(paths.resolve(core::pathFromUtf8(settings.editorSystems))), manager));
        run(session, SceneOperation::Open, core::pathToUtf8(paths.resolve(core::pathFromUtf8(settings.startScene))));
        CHECK(!session.dirty()); CHECK(session.editorScene().entities().size() == 2);
        CHECK(manager.list().size() == 1 && manager.list()[0]->name == "ProjectEditor");
        run(session, SceneOperation::SaveAs, "saved.scene");
        CHECK(std::filesystem::is_regular_file(paths.assets / "saved.scene"));
        run(session, SceneOperation::Play); CHECK(game->target == &session.activeContext().scene);
        run(session, SceneOperation::Stop); CHECK(!game->attached());
        manager.unbind(); manager.shutdown(); core::details::ServiceRegistry::shutdownAll();
    }
    auto invalid = first; invalid.projectPath = "Missing"; CHECK(invalid.save());
    bool rejected = false; try { ProjectSettings::load(firstFile); } catch (const std::exception&) { rejected = true; }
    CHECK(rejected); CHECK(first.save());
    invalid = first; invalid.startScene = "Content/missing.scene"; CHECK(invalid.save());
    rejected = false; try { ProjectSettings::load(firstFile); } catch (const std::exception&) { rejected = true; }
    CHECK(rejected); CHECK(first.save());
    invalid = first; invalid.startScene.clear(); CHECK(invalid.save());
    CHECK(ProjectSettings::load(firstFile).startScene.empty());
    CHECK(std::filesystem::current_path() == cwd);
    CHECK(ProjectSettings::load("CSyren.csyrenproj").startScene == "assets/test_scene.scene");
}

void inspectorTransaction()
{
    MemoryStorage storage;
    auto& session = core::details::ServiceRegistry::create<SceneSession>(storage);
    core::details::ServiceRegistry::create<EditorSelection>();
    EditorRegistry::resolveAll();
    run(session, SceneOperation::CreateEntity);
    const auto key = lastObject(session);
    run(session, SceneOperation::SaveAs, "ui");
    ImGui::CreateContext();
    auto& io = ImGui::GetIO(); io.IniFilename = nullptr;
    io.DisplaySize = ImVec2(1000, 800); io.DeltaTime = 1.0f / 60;
    unsigned char* pixels{}; int width{}, height{};
    io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
    ImVec2 min, max;
    auto frame = [&] {
        ImGui::NewFrame();
        ImGui::SetNextWindowPos(ImVec2(20, 20)); ImGui::SetNextWindowSize(ImVec2(700, 300));
        ImGui::Begin("Inspector transaction");
        auto transform = session.editorScene().getComponent<Transform>(session.resolve(key));
        reflection::MetaAny value(transform->position);
        ValueEditContext context{ session, key, reflection::ComponentFamily::getID<Transform>(), { "position"_hs.id } };
        EditorRegistry::get(value.type())->draw(value, "Position", context);
        min = ImGui::GetItemRectMin(); max = ImGui::GetItemRectMax();
        ImGui::End();
        if (!ImGui::IsAnyItemActive()) session.finishEdit();
        ImGui::Render();
    };
    frame();
    const ImVec2 position(min.x + 30, (min.y + max.y) / 2);
    io.AddMousePosEvent(position.x, position.y); frame();
    io.AddMouseButtonEvent(0, true); frame();
    io.AddMousePosEvent(position.x + 60, position.y); frame();
    io.AddMousePosEvent(position.x + 120, position.y); frame();
    io.AddMouseButtonEvent(0, false); frame();
    CHECK(session.dirty());
    const auto changed = session.editorScene().getComponent<Transform>(session.resolve(key))->position;
    CHECK(changed.x != 0);
    run(session, SceneOperation::Undo);
    CHECK(!session.dirty());
    CHECK(session.editorScene().getComponent<Transform>(session.resolve(key))->position.x == 0);
    run(session, SceneOperation::Redo);
    CHECK(session.editorScene().getComponent<Transform>(session.resolve(key))->position.x == changed.x);
    io.AddMousePosEvent(position.x, position.y); frame();
    io.AddMouseButtonEvent(0, true); frame();
    io.AddMousePosEvent(position.x + 100, position.y); frame();
    io.AddMousePosEvent(position.x + 150, position.y); frame();
    io.AddKeyEvent(ImGuiKey_Escape, true); frame();
    io.AddKeyEvent(ImGuiKey_Escape, false); io.AddMouseButtonEvent(0, false); frame();
    CHECK(session.editorScene().getComponent<Transform>(session.resolve(key))->position.x == changed.x);
    run(session, SceneOperation::Undo); CHECK(!session.dirty());
    // Draw both UI windows against Edit and Playing without any renderer backend.
    auto drawWindows = [&] {
        ImGui::NewFrame();
        Services::get<EditorSelection>()->select(session.resolve(key));
        InspectorWindow inspector; SceneWorkflowWindow workflow;
        if (ImGui::BeginMainMenuBar()) { workflow.menus(); ImGui::EndMainMenuBar(); }
        workflow.render(); inspector.render();
        ImGui::Render();
    };
    drawWindows(); run(session, SceneOperation::Play); drawWindows(); run(session, SceneOperation::Stop);
    ImGui::DestroyContext();
    core::details::ServiceRegistry::shutdownAll();
}

void reverseDepthProjection()
{
    Camera camera;
    CHECK(camera.near == 0.1f && camera.far == 1000.0f);
    for (auto type : { Perspective, Orthographic })
    {
        camera.projection = type;
        for (bool legacy : { false, true })
        {
            camera.near = legacy ? 1000.0f : 0.1f;
            camera.far = legacy ? 0.1f : 1000.0f;
            const auto matrix = camera.projectionMatrix();
            auto depth = [&](float z) {
                return DirectX::XMVectorGetZ(DirectX::XMVector3TransformCoord(
                    DirectX::XMVectorSet(0, 0, z, 1), matrix));
            };
            CHECK(std::abs(depth(0.1f) - 1.0f) < 1e-5f);
            CHECK(std::abs(depth(1000.0f)) < 1e-5f);
            CHECK(depth(0.05f) > 1.0f && depth(2000.0f) < 0.0f);
            // With GREATER_EQUAL the closer surface wins in either draw order.
            const float closeDepth = depth(2.0f), distantDepth = depth(10.0f);
            CHECK(closeDepth > distantDepth && distantDepth > 0.0f);
            for (bool closeFirst : { false, true })
            {
                float buffer = 0.0f;
                for (float sample : { closeFirst ? closeDepth : distantDepth,
                    closeFirst ? distantDepth : closeDepth })
                    if (sample >= buffer) buffer = sample;
                CHECK(buffer == closeDepth);
            }
        }
    }
    EditorViewportState editor;
    const auto point = DirectX::XMVector3TransformCoord(DirectX::XMVectorSet(0, 0, editor.camera.near, 1),
        editor.camera.projectionMatrix());
    CHECK(std::abs(DirectX::XMVectorGetZ(point) - 1.0f) < 1e-5f);
}

static void viewportPicking()
{
    using namespace DirectX;
    const auto fallbackDown = creationPosition({ { 0, 2, 0 }, { 0, -.6f, .8f }, 100 });
    CHECK(std::abs(fallbackDown.y + 1) < 1e-5f && std::abs(fallbackDown.z - 4) < 1e-5f);
    render::MeshBuilder triangle;
    triangle.addVertex({ 0, 0, 0 }).addVertex({ 2, 0, 0 }).addVertex({ 0, 2, 0 }).addTriangle(0, 1, 2);
    const auto geometry = triangle.build();
    float surfaceDistance;
    CHECK(intersectMesh({ { .5f, .5f, 0 }, { 0, 0, 1 }, 100 }, geometry.bounds,
        geometry.positions, geometry.indices, XMMatrixTranslation(0, 0, 12), surfaceDistance));
    CHECK(std::abs(surfaceDistance - 12) < 1e-5f);
    CHECK(intersectMesh({ { -.5f, 1.f, 0 }, { 0, 0, 1 }, 100 }, geometry.bounds,
        geometry.positions, geometry.indices, XMMatrixScaling(-2, 3, .5f) * XMMatrixTranslation(0, 0, 7), surfaceDistance));
    CHECK(std::abs(surfaceDistance - 7) < 1e-5f);
    // Nearest surface wins in either entity iteration order, even when local
    // distances differ dramatically because of scaling.
    for (bool nearFirst : { false, true })
    {
        float nearest = 100;
        int selected = -1;
        for (int index : { nearFirst ? 0 : 1, nearFirst ? 1 : 0 })
        {
            const auto world = index == 0 ?
                XMMatrixScaling(1, 1, .01f) * XMMatrixTranslation(0, 0, 4) :
                XMMatrixScaling(1, 1, 100.f) * XMMatrixTranslation(0, 0, 12);
            float candidate;
            if (intersectMesh({ { .5f, .5f, 0 }, { 0, 0, 1 }, 100 }, geometry.bounds,
                geometry.positions, geometry.indices, world, candidate) && candidate < nearest)
            { nearest = candidate; selected = index; }
        }
        CHECK(selected == 0 && std::abs(nearest - 4) < 1e-5f);
    }
    CHECK(!intersectMesh({ { 1.5f, 1.5f, 0 }, { 0, 0, 1 }, 100 }, geometry.bounds,
        geometry.positions, geometry.indices, XMMatrixTranslation(0, 0, 12), surfaceDistance));
    CHECK(!intersectMesh({ { .5f, .5f, 0 }, { 0, 0, 1 }, 10 }, geometry.bounds,
        geometry.positions, geometry.indices, XMMatrixTranslation(0, 0, 12), surfaceDistance));
    render::MeshBuilder cube;
    for (int i = 0; i < 8; ++i)
        cube.addVertex({ (i&1) ? .5f : -.5f, (i&2) ? .5f : -.5f, (i&4) ? .5f : -.5f });
    for (const auto face : { std::array<uint16_t, 4>{ 0, 1, 3, 2 }, { 4, 6, 7, 5 },
        { 0, 2, 6, 4 }, { 1, 5, 7, 3 }, { 0, 4, 5, 1 }, { 2, 3, 7, 6 } })
        cube.addTriangle(face[0], face[1], face[2]).addTriangle(face[0], face[2], face[3]);
    const auto cubeGeometry = cube.build();
    for (bool nearFirst : { false, true })
    {
        float nearest = 100;
        int selected = -1;
        for (int index : { nearFirst ? 0 : 1, nearFirst ? 1 : 0 })
        {
            float candidate;
            if (intersectMesh({ { 0, 0, 0 }, { 0, 0, 1 }, 100 }, cubeGeometry.bounds,
                cubeGeometry.positions, cubeGeometry.indices, XMMatrixTranslation(0, 0, index == 0 ? 4.f : 12.f), candidate) && candidate < nearest)
            { nearest = candidate; selected = index; }
        }
        CHECK(selected == 0 && std::abs(nearest - 3.5f) < 1e-5f);
    }
    const auto fallback = creationPosition({ { 1, 2, 3 }, { 0, 0, 1 }, 100 });
    CHECK(fallback.x == 1 && fallback.y == 2 && fallback.z == 8);
    MemoryStorage storage;
    SceneSession session(storage);
    SceneRequest request{ SceneOperation::CreateEntity, session.documentId() };
    request.position = math::Vector3{ 3, 4, 5 };
    session.enqueue(request); session.processRequests();
    CHECK(session.error().empty());
    const auto created = session.takeCreatedEntity();
    CHECK(created != Entity::invalidID);
    const auto key = session.keyFor(created);
    CHECK(session.editorScene().getComponent<Transform>(created)->position.exactEqual(request.position));
    run(session, SceneOperation::Undo);
    CHECK(session.resolve(key) == Entity::invalidID);
    run(session, SceneOperation::Redo);
    CHECK(session.editorScene().getComponent<Transform>(session.resolve(key))->position.exactEqual(request.position));
    run(session, SceneOperation::SaveAs, "placed");
    run(session, SceneOperation::Open, "placed");
    CHECK(session.editorScene().getComponent<Transform>(session.resolve(lastObject(session)))->position.exactEqual(request.position));
    render::MeshBuilder builder;
    builder.addVertex({ -1, -2, -3 }).addVertex({ 1, 2, 3 });
    const auto bounds = builder.build().bounds;
    CHECK(bounds.valid && bounds.min.y == -2 && bounds.max.z == 3);
    CHECK(!render::MeshBuilder{}.build().bounds.valid);
    for (auto projection : { Perspective, Orthographic })
    {
        Camera camera; camera.projection = projection;
        const auto ray = viewportRay(0, 0, camera.projectionMatrix());
        CHECK(std::abs(ray.origin.z - .1f) < 1e-4f);
        CHECK(std::abs(ray.direction.z - 1.f) < 1e-5f);
        float distance;
        CHECK(intersectBounds(ray, bounds, XMMatrixTranslation(0, 0, 10), distance));
        CHECK(std::abs(distance - 6.9f) < 1e-3f);
        CHECK(intersectBounds(ray, bounds, XMMatrixScaling(-2, 3, .5f) * XMMatrixTranslation(0, 0, 5), distance));
        CHECK(std::abs(distance - 3.4f) < 1e-3f);
        CHECK(!intersectBounds(ray, bounds, XMMatrixTranslation(20, 0, 10), distance));
        CHECK(!intersectBounds(ray, bounds, XMMatrixTranslation(0, 0, -10), distance));
        CHECK(!intersectBounds(ray, bounds, XMMatrixScaling(0, 1, 1), distance));
    }
    XMFLOAT4 a{ 0, 0, 2, 1 }, b{ 0, 0, .5f, 1 };
    CHECK(clipBoundsEdge(a, b) && std::abs(a.z - 1) < 1e-6f);
    a = { 2, 0, .5f, 1 }; b = { 3, 0, .5f, 1 };
    CHECK(!clipBoundsEdge(a, b));
    EditorSelection selection; selection.synchronize(1); selection.select(42);
    selection.synchronize(1); CHECK(selection.selected() == 42);
    selection.synchronize(2); CHECK(!selection.hasSelection());
}

struct ComponentResource
{
    render::MaterialHandle material;
    static void describe()
    {
        render::describeResourceHandleMeta();
        reflection::MetaFactory<ComponentResource>{}.type("ComponentResource"_hs)
            .data<&ComponentResource::material>("material"_hs);
    }
};

static void componentResourceWorkflow()
{
    auto& resources = core::details::ServiceRegistry::create<render::ResourceManager>();
    // This CPU-only material is never rendered; its shader handle is opaque here.
    const auto material = resources.createMaterial("component-test-material", render::ShaderHandle{ 0 }, render::MaterialStateDesc{});
    CHECK(material);
    Serializer serializer;
    ArchiveSceneStorage storage(serializer);
    SceneSession session(storage);
    run(session, SceneOperation::CreateEntity);
    const auto id = session.takeCreatedEntity();
    session.editorScene().addComponent<ComponentResource>(id)->material = material;
    SceneRequest request{ SceneOperation::RemoveComponent, session.documentId() };
    request.entity = session.keyFor(id); request.component = reflection::ComponentFamily::getID<ComponentResource>();
    session.enqueue(request); session.processRequests(); CHECK(session.error().empty());
    CHECK(!session.editorScene().hasComponent<ComponentResource>(id));
    run(session, SceneOperation::Undo);
    CHECK(session.editorScene().getComponent<ComponentResource>(id)->material == material);
    request.operation = SceneOperation::DuplicateEntity;
    session.enqueue(request); session.processRequests(); CHECK(session.error().empty());
    const auto copyKey = session.keyFor(session.takeCreatedEntity());
    CHECK(session.editorScene().getComponent<ComponentResource>(session.resolve(copyKey))->material == material);
    request.operation = SceneOperation::DeleteEntity; request.entity = copyKey;
    session.enqueue(request); session.processRequests(); CHECK(session.error().empty());
    run(session, SceneOperation::Undo);
    CHECK(session.editorScene().getComponent<ComponentResource>(session.resolve(copyKey))->material == material);
    core::details::ServiceRegistry::shutdownAll();
}

static void componentWorkflow()
{
    MemoryStorage storage;
    SceneSession session(storage);
    run(session, SceneOperation::CreateEntity);
    const auto id = session.takeCreatedEntity();
    const auto key = session.keyFor(id);
    const auto instance = session.editorContext().instance;
    const auto change = [&](SceneOperation operation, size_t family) {
        SceneRequest request{ operation, session.documentId() };
        request.entity = key; request.component = family;
        session.enqueue(request); session.processRequests();
        CHECK(session.error().empty());
        CHECK(session.editorContext().instance == instance);
    };
    const auto bodyFamily = reflection::ComponentFamily::getID<physics::RigidBody>();
    change(SceneOperation::AddComponent, bodyFamily);
    CHECK(session.editorScene().hasComponent<physics::RigidBody>(id));
    CHECK(session.editorScene().getComponent<physics::RigidBody>(id)->mass == 1);
    SceneRequest duplicate{ SceneOperation::AddComponent, session.documentId() };
    duplicate.entity = key; duplicate.component = bodyFamily;
    session.enqueue(duplicate); session.processRequests();
    CHECK(!session.error().empty());
    CHECK(session.editorScene().getComponent<physics::RigidBody>(id)->mass == 1);
    session.editorScene().getComponent<physics::RigidBody>(id)->mass = 17;
    change(SceneOperation::RemoveComponent, bodyFamily);
    CHECK(!session.editorScene().hasComponent<physics::RigidBody>(id));
    run(session, SceneOperation::Undo);
    CHECK(session.editorScene().getComponent<physics::RigidBody>(id)->mass == 17);
    run(session, SceneOperation::Redo);
    CHECK(!session.editorScene().hasComponent<physics::RigidBody>(id));
    run(session, SceneOperation::Undo);
    run(session, SceneOperation::Undo);
    CHECK(!session.editorScene().hasComponent<physics::RigidBody>(id));
    run(session, SceneOperation::Redo);
    CHECK(session.editorScene().getComponent<physics::RigidBody>(id)->mass == 1);
    // A reference is archived using document keys and resolved when undo runs.
    const auto target = session.editorScene().createEntity("reference target");
    const auto targetKey = session.keyFor(target);
    const auto linkFamily = reflection::ComponentFamily::getID<Link>();
    change(SceneOperation::AddComponent, linkFamily);
    session.editorScene().getComponent<Link>(id)->target = target;
    change(SceneOperation::RemoveComponent, linkFamily);
    run(session, SceneOperation::Undo);
    CHECK(session.editorScene().getComponent<Link>(id)->target == session.resolve(targetKey));
    run(session, SceneOperation::SaveAs, "components");
    run(session, SceneOperation::Open, "components");
    const auto loaded = session.resolve(lastObject(session));
    CHECK(session.editorScene().hasComponent<Link>(loaded));
    CHECK(session.editorScene().hasComponent<physics::RigidBody>(loaded));
    const auto referenced = session.editorScene().getComponent<Link>(loaded)->target;
    CHECK(session.editorScene().entities().try_get(referenced)->name == "reference target");
    run(session, SceneOperation::Play);
    SceneRequest blocked{ SceneOperation::RemoveComponent, session.documentId() };
    blocked.entity = session.keyFor(loaded); blocked.component = linkFamily;
    session.enqueue(blocked); session.processRequests();
    CHECK(!session.error().empty() && session.editorScene().hasComponent<Link>(loaded));
    run(session, SceneOperation::Stop);
}

static void branchWorkflow()
{
    MemoryStorage storage;
    SceneSession session(storage);
    run(session, SceneOperation::CreateEntity);
    const auto key = session.keyFor(session.takeCreatedEntity());
    const auto child = session.editorScene().createEntity("child", session.resolve(key));
    session.editorScene().addComponent<Transform>(child)->position = math::Vector3{ 1, 2, 3 };
    const auto childKey = session.keyFor(child);
    const auto external = session.editorScene().createEntity("external");
    session.editorScene().addComponent<Transform>(external);
    const auto externalKey = session.keyFor(external);
    session.editorScene().addComponent<Link>(session.resolve(key))->target = child;
    session.editorScene().addComponent<Link>(child)->target = external;
    run(session, SceneOperation::SaveAs, "branch");
    const auto action = [&](SceneOperation operation, EntityKey target) {
        SceneRequest request{ operation, session.documentId() }; request.entity = target;
        session.enqueue(request); session.processRequests(); CHECK(session.error().empty());
    };
    action(SceneOperation::DuplicateEntity, childKey);
    const auto nestedCopy = session.takeCreatedEntity();
    CHECK(session.editorScene().entities().try_get(nestedCopy)->parent == session.resolve(key));
    CHECK(session.editorScene().getComponent<Link>(nestedCopy)->target == session.resolve(externalKey));
    run(session, SceneOperation::Undo);
    CHECK(!session.dirty());
    action(SceneOperation::DuplicateEntity, key);
    const auto copy = session.takeCreatedEntity();
    const auto copyKey = session.keyFor(copy);
    CHECK(copy != session.resolve(key));
    CHECK(session.editorScene().entities().try_get(copy)->name == "New object (Copy)");
    const auto copiedChild = session.editorScene().entities().try_get(copy)->children.at(0);
    CHECK(copiedChild != session.resolve(childKey));
    CHECK(session.editorScene().getComponent<Link>(copy)->target == copiedChild);
    CHECK(session.editorScene().getComponent<Link>(copiedChild)->target == session.resolve(externalKey));
    CHECK(session.editorScene().getComponent<Transform>(copiedChild)->position.exactEqual(math::Vector3{ 1, 2, 3 }));
    CHECK(session.editorScene().entities().try_get(copiedChild)->parent == copy);
    action(SceneOperation::DeleteEntity, copyKey);
    CHECK(session.resolve(copyKey) == Entity::invalidID);
    CHECK(session.editorScene().entities().size() == 4);
    run(session, SceneOperation::Undo);
    CHECK(session.takeCreatedEntity() == session.resolve(copyKey));
    CHECK(session.editorScene().entities().try_get(session.resolve(copyKey))->children.size() == 1);
    run(session, SceneOperation::Undo);
    CHECK(session.resolve(copyKey) == Entity::invalidID && !session.dirty());
    CHECK(session.editorScene().getComponent<Link>(session.resolve(key))->target == session.resolve(childKey));
    run(session, SceneOperation::Redo);
    CHECK(session.resolve(copyKey) != Entity::invalidID);
    run(session, SceneOperation::Save);
    run(session, SceneOperation::Open, "branch");
    CHECK(session.editorScene().entities().size() == 6);
    // Deleting a branch referenced by a surviving required reference is rejected
    // before committing, rather than producing a document that cannot be saved.
    const auto original = session.resolve(lastObject(session));
    const auto originalKey = session.keyFor(original);
    session.editorScene().addComponent<Link>(session.editorScene().createEntity("referrer"))->target = original;
    const auto count = session.editorScene().entities().size();
    SceneRequest blocked{ SceneOperation::DeleteEntity, session.documentId() }; blocked.entity = originalKey;
    session.enqueue(blocked); session.processRequests();
    CHECK(!session.error().empty() && session.editorScene().entities().size() == count);
    CHECK(session.editorScene().entities().contains(session.resolve(originalKey)));
}

static void completeEditingWorkflow()
{
    MemoryStorage storage;
    SceneSession session(storage);
    run(session, SceneOperation::SaveAs, "complete");
    run(session, SceneOperation::CreateEntity);
    const auto key = session.keyFor(session.takeCreatedEntity());
    SceneRequest add{ SceneOperation::AddComponent, session.documentId() };
    add.entity = key; add.component = reflection::ComponentFamily::getID<physics::BoxCollider>();
    session.enqueue(add); session.processRequests(); CHECK(session.error().empty());
    SceneRequest duplicate{ SceneOperation::DuplicateEntity, session.documentId() }; duplicate.entity = key;
    session.enqueue(duplicate); session.processRequests(); CHECK(session.error().empty());
    const auto copyKey = session.keyFor(session.takeCreatedEntity());
    CHECK(session.editorScene().hasComponent<physics::BoxCollider>(session.resolve(copyKey)));
    SceneRequest remove{ SceneOperation::DeleteEntity, session.documentId() }; remove.entity = copyKey;
    session.enqueue(remove); session.processRequests(); CHECK(session.error().empty());
    for (int i = 0; i < 4; ++i) run(session, SceneOperation::Undo);
    CHECK(session.editorScene().entities().size() == 1 && !session.dirty());
    run(session, SceneOperation::Save);
    run(session, SceneOperation::Open, "complete");
    CHECK(session.editorScene().entities().size() == 1);
}

#include "scene_pipeline_cases.h"

class SceneWorkflow : public ::testing::Test
{
protected:
    static void SetUpTestCase()
    {
        MetaCommonDescriber::describe();
        REGISTER_COMPONENT(Transform);
        REGISTER_COMPONENT(Link);
        using physics::RigidBody;
        using physics::BoxCollider;
        REGISTER_COMPONENT(RigidBody);
        REGISTER_COMPONENT(BoxCollider);
        REGISTER_COMPONENT(ComponentResource);
        REGISTER_COMPONENT(Camera);
    }
    void TearDown() override
    {
        if (ImGui::GetCurrentContext()) ImGui::DestroyContext();
        core::details::ServiceRegistry::shutdownAll();
    }
};

TEST_F(SceneWorkflow, pipeline_each_undo_redo_state) { ASSERT_NO_THROW(pipelineEachState()); }
TEST_F(SceneWorkflow, pipeline_branch_failure_atomicity) { ASSERT_NO_THROW(pipelineBranchFailures()); }
TEST_F(SceneWorkflow, pipeline_invalid_and_play_requests) { ASSERT_NO_THROW(pipelineInvalidRequests()); }
TEST_F(SceneWorkflow, pipeline_disk_roundtrip) { ASSERT_NO_THROW(pipelineDiskRoundtrip()); }
TEST_F(SceneWorkflow, pipeline_nested_reference_cycles) { ASSERT_NO_THROW(pipelineNestedReferences()); }
TEST_F(SceneWorkflow, pipeline_pending_edits_and_redo_branch) { ASSERT_NO_THROW(pipelinePendingEdits()); }
TEST_F(SceneWorkflow, complete_editing_workflow) { ASSERT_NO_THROW(completeEditingWorkflow()); }
TEST_F(SceneWorkflow, component_add_remove) { ASSERT_NO_THROW(componentWorkflow()); }
TEST_F(SceneWorkflow, component_resource_references) { ASSERT_NO_THROW(componentResourceWorkflow()); }
TEST_F(SceneWorkflow, branch_hierarchy_and_references) { ASSERT_NO_THROW(branchWorkflow()); }
TEST_F(SceneWorkflow, document_save_reload_play_history) { ASSERT_NO_THROW(documentWorkflow()); }
TEST_F(SceneWorkflow, import_is_not_replace) { ASSERT_NO_THROW(importWorkflow()); }
TEST_F(SceneWorkflow, system_lifecycle) { ASSERT_NO_THROW(systemLifecycle()); }
TEST_F(SceneWorkflow, systems_configuration) { ASSERT_NO_THROW(systemsConfiguration()); }
TEST_F(SceneWorkflow, system_graph_previews) { ASSERT_NO_THROW(systemGraphPreviews()); }
TEST_F(SceneWorkflow, physics_play_stop) { ASSERT_NO_THROW(physicsLifecycle()); }
TEST_F(SceneWorkflow, atomic_file_save) { ASSERT_NO_THROW(fileStorage()); }
TEST_F(SceneWorkflow, project_configuration) { ASSERT_NO_THROW(projectConfiguration()); }
TEST_F(SceneWorkflow, inspector_transactions) { ASSERT_NO_THROW(inspectorTransaction()); }
TEST_F(SceneWorkflow, reverse_z) { ASSERT_NO_THROW(reverseDepthProjection()); }
TEST_F(SceneWorkflow, viewport_picking_and_creation) { ASSERT_NO_THROW(viewportPicking()); }
TEST_F(SceneWorkflow, startup_diagnostics) { ASSERT_NO_THROW(startupDiagnostics()); }
TEST_F(SceneWorkflow, startup_arguments) { ASSERT_NO_THROW(startupArguments()); }
TEST_F(SceneWorkflow, render_environment) { ASSERT_NO_THROW(renderEnvironmentDefaults()); }
TEST_F(SceneWorkflow, editor_camera_worker) { ASSERT_NO_THROW(editorCameraWorkerExecution()); }

TEST_F(SceneWorkflow, FocusFitsBoundsAndPreservesCameraOrientation)
{
    using namespace DirectX;
    math::MeshBounds local;
    local.include({ -2, -1, -3 }); local.include({ 2, 1, 3 });
    const auto bounds = transformedBounds(local, XMMatrixScaling(-2, 3, .5f) *
        XMMatrixRotationRollPitchYaw(.3f, .7f, 0) * XMMatrixTranslation(30, 12, -20));
    for (const float aspect : { .4f, 1.f, 2.f })
        for (const auto projection : { Perspective, Orthographic })
        {
            SCOPED_TRACE(aspect);
            SCOPED_TRACE(static_cast<int>(projection));
            EditorViewportState view;
            view.camera.aspectRatio = aspect; view.camera.projection = projection;
            view.transform.rotation = XMQuaternionRotationRollPitchYaw(.2f, -.7f, 0);
            const auto rotation = view.transform.rotation;
            ASSERT_TRUE(focusBounds(view, bounds));
            EXPECT_TRUE(XMVector4Equal(rotation, view.transform.rotation));
            const auto vp = XMMatrixMultiply(XMMatrixInverse(nullptr, view.transform.world()), view.camera.projectionMatrix());
            for (int i = 0; i < 8; ++i)
            {
                const math::Vector3 p(XMVector3TransformCoord(XMVectorSet(
                    (i&1) ? bounds.max.x : bounds.min.x, (i&2) ? bounds.max.y : bounds.min.y,
                    (i&4) ? bounds.max.z : bounds.min.z, 1), vp));
                EXPECT_LE(std::abs(p.x), 1.f); EXPECT_LE(std::abs(p.y), 1.f);
                EXPECT_GE(p.z, 0.f); EXPECT_LE(p.z, 1.f);
            }
        }
}

TEST_F(SceneWorkflow, FocusHandlesEmptyPointAndLargeBounds)
{
    EditorViewportState view;
    const auto initial = view.transform.position;
    EXPECT_FALSE(focusBounds(view, {}));
    EXPECT_TRUE(view.transform.position.exactEqual(initial));
    math::MeshBounds point; point.include({ 0, 0, 0 });
    ASSERT_TRUE(focusBounds(view, point));
    EXPECT_TRUE(std::isfinite(view.transform.position.z));
    math::MeshBounds huge; huge.include({ -10000, -10000, -10000 }); huge.include({ 10000, 10000, 10000 });
    ASSERT_TRUE(focusBounds(view, huge));
    EXPECT_GT(view.camera.far, 1000.f);
    EXPECT_GT(view.camera.far, view.camera.near);
}

TEST_F(SceneWorkflow, GizmoGestureCommitsOneUndoAndEscapeRestoresValues)
{
    MemoryStorage storage; SceneSession session(storage);
    run(session, SceneOperation::CreateEntity);
    const auto id = session.takeCreatedEntity(); const auto key = session.keyFor(id);
    run(session, SceneOperation::SaveAs, "gizmo");
    TransformGesture gesture;
    ASSERT_TRUE(gesture.begin(session, id));
    ASSERT_TRUE(gesture.move(session, { 1, 0, 0 }));
    ASSERT_TRUE(gesture.move(session, { 2, 0, 0 }));
    ASSERT_TRUE(gesture.move(session, { 3, 0, 0 }));
    gesture.finish(session);
    EXPECT_FLOAT_EQ(session.editorScene().getComponent<Transform>(id)->position.x, 3);
    run(session, SceneOperation::Undo);
    EXPECT_FLOAT_EQ(session.editorScene().getComponent<Transform>(id)->position.x, 0);
    EXPECT_FALSE(session.dirty());
    run(session, SceneOperation::Redo);
    EXPECT_FLOAT_EQ(session.editorScene().getComponent<Transform>(id)->position.x, 3);
    run(session, SceneOperation::Save);
    ASSERT_TRUE(gesture.begin(session, id));
    ASSERT_TRUE(gesture.move(session, { 0, 5, 0 }));
    gesture.cancel(session);
    EXPECT_TRUE(session.editorScene().getComponent<Transform>(id)->position.exactEqual(math::Vector3{3,0,0}));
    EXPECT_FALSE(session.dirty());
    ASSERT_TRUE(gesture.begin(session, id)); gesture.finish(session);
    EXPECT_FALSE(session.dirty()); // no movement must not record a command
    run(session, SceneOperation::Play);
    EXPECT_FALSE(gesture.begin(session, session.resolve(key)));
    run(session, SceneOperation::Stop);
}

TEST_F(SceneWorkflow, AxisDragRejectsParallelRaysAndPreservesDirection)
{
    float value;
    EXPECT_FALSE(rayAxisParameter({ {0,0,-5}, {0,0,1}, 100 }, {0,0,0}, {0,0,1}, value));
    ASSERT_TRUE(rayAxisParameter({ {2,3,-5}, {0,0,1}, 100 }, {0,0,0}, {1,0,0}, value));
    EXPECT_FLOAT_EQ(value, 2);
}

TEST_F(SceneWorkflow, OldGizmoCannotCancelAnEditInAnotherDocument)
{
    MemoryStorage storage; SceneSession session(storage);
    run(session, SceneOperation::CreateEntity);
    const auto id = session.takeCreatedEntity(); run(session, SceneOperation::SaveAs, "before focus");
    TransformGesture gesture;
    ASSERT_TRUE(gesture.begin(session, id));
    ASSERT_TRUE(gesture.move(session, { 1, 0, 0 }));
    run(session, SceneOperation::Open, "before focus", true);
    const auto key = lastObject(session);
    position(session, key, math::Vector3{ 9, 8, 7 }, false);
    EXPECT_FALSE(gesture.valid(session, session.resolve(key)));
    gesture.cancel(session);
    EXPECT_TRUE(session.editorScene().getComponent<Transform>(session.resolve(key))->position.exactEqual(math::Vector3{9,8,7}));
    EXPECT_TRUE(session.dirty());
    session.finishEdit(); run(session, SceneOperation::Undo);
    EXPECT_FALSE(session.dirty());
}

TEST_F(SceneWorkflow, ImGuiGizmoMouseDragUsesSingleUndo)
{
    MemoryStorage storage; SceneSession session(storage);
    run(session, SceneOperation::CreateEntity);
    const auto id = session.takeCreatedEntity(); run(session, SceneOperation::SaveAs, "mouse gizmo");
    ImGui::CreateContext();
    auto& io = ImGui::GetIO(); io.IniFilename = nullptr;
    io.DisplaySize = ImVec2(800,600); io.DeltaTime = 1.f/60;
    unsigned char* pixels; int w, h; io.Fonts->GetTexDataAsRGBA32(&pixels, &w, &h);
    EditorViewportState view; view.camera.aspectRatio = 4.f/3;
    view.transform.position = math::Vector3{0,0,-5};
    const auto vp = DirectX::XMMatrixMultiply(DirectX::XMMatrixInverse(nullptr, view.transform.world()), view.camera.projectionMatrix());
    TransformGizmo gizmo;
    const auto frame = [&] {
        ImGui::NewFrame();
        gizmo.render(session, id, view.camera, vp, {0,0}, {800,600}, true);
        if (!gizmo.active()) session.finishEdit();
        ImGui::Render();
    };
    io.AddMousePosEvent(464,300); frame();
    io.AddMouseButtonEvent(0,true); frame();
    ASSERT_TRUE(gizmo.active());
    io.AddMousePosEvent(500,300); frame();
    io.AddMousePosEvent(544,300); frame();
    io.AddMouseButtonEvent(0,false); frame();
    EXPECT_FALSE(gizmo.active());
    const auto changed = session.editorScene().getComponent<Transform>(id)->position;
    EXPECT_GT(changed.x, .5f); EXPECT_FLOAT_EQ(changed.y, 0); EXPECT_FLOAT_EQ(changed.z, 0);
    run(session, SceneOperation::Undo);
    EXPECT_TRUE(session.editorScene().getComponent<Transform>(id)->position.exactEqual(math::Vector3{0,0,0}));
    EXPECT_FALSE(session.dirty());
    run(session, SceneOperation::Redo);
    EXPECT_TRUE(session.editorScene().getComponent<Transform>(id)->position.exactEqual(changed));
    run(session, SceneOperation::Save);
    io.AddMousePosEvent(544,300); frame();
    io.AddMouseButtonEvent(0,true); frame();
    ASSERT_TRUE(gizmo.active());
    io.AddMousePosEvent(584,300); frame();
    io.AddKeyEvent(ImGuiKey_Escape, true); frame();
    EXPECT_FALSE(gizmo.active());
    EXPECT_TRUE(session.editorScene().getComponent<Transform>(id)->position.exactEqual(changed));
    EXPECT_FALSE(session.dirty());
    io.AddKeyEvent(ImGuiKey_Escape, false); io.AddMouseButtonEvent(0,false); frame();
}

#include "gizmo_transform_cases.h"
#include "viewport_cases.h"
