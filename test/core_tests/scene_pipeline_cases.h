#pragma once

// Integration cases use the real SceneSession, command history, ECS and archive.
// Only storage I/O/failures are substituted by MemoryStorage where specified.
static std::string pipelineSnapshot(SceneStorage& storage, SceneSession& session)
{
    std::string archive;
    CHECK(storage.snapshot(session.editorScene(), archive));
    return archive;
}

static void pipelineAction(SceneSession& session, SceneOperation operation,
    EntityKey entity = 0, size_t component = 0)
{
    SceneRequest request{ operation, session.documentId() };
    request.entity = entity; request.component = component;
    session.enqueue(request); session.processRequests(); CHECK(session.error().empty());
}

static void pipelineEachState()
{
    MemoryStorage storage;
    SceneInstance seed;
    const auto existing = seed.scene().createEntity("existing object");
    seed.scene().addComponent<Transform>(existing)->position = math::Vector3{ -3, 2, 1 };
    CHECK(storage.write("pipeline", seed.scene()));
    SceneSession session(storage);
    run(session, SceneOperation::Open, "pipeline");
    std::vector<std::string> states{ pipelineSnapshot(storage, session) };
    const auto checkpoint = [&] { states.push_back(pipelineSnapshot(storage, session)); CHECK(session.dirty()); };
    CHECK(!session.canUndo() && !session.canRedo());
    SceneRequest create{ SceneOperation::CreateEntity, session.documentId() };
    create.position = math::Vector3{ 3, 4, 5 };
    session.enqueue(create); session.processRequests(); CHECK(session.error().empty());
    const auto original = session.keyFor(session.takeCreatedEntity()); checkpoint();
    const auto family = reflection::ComponentFamily::getID<physics::RigidBody>();
    pipelineAction(session, SceneOperation::AddComponent, original, family); checkpoint();
    auto body = session.editorScene().getComponent<physics::RigidBody>(session.resolve(original));
    const auto before = body->mass; body->mass = 13;
    session.preview(original, family, { "mass"_hs.id }, before, 13.f, true); checkpoint();
    pipelineAction(session, SceneOperation::DuplicateEntity, original);
    const auto copy = session.keyFor(session.takeCreatedEntity());
    CHECK(session.editorScene().getComponent<physics::RigidBody>(session.resolve(copy))->mass == 13); checkpoint();
    pipelineAction(session, SceneOperation::RemoveComponent, copy, family); checkpoint();
    pipelineAction(session, SceneOperation::DeleteEntity, original); checkpoint();
    CHECK(session.resolve(original) == Entity::invalidID);
    for (size_t i = states.size() - 1; i > 0; --i)
    {
        run(session, SceneOperation::Undo);
        CHECK(pipelineSnapshot(storage, session) == states[i-1]);
        CHECK(session.dirty() == (i > 1));
    }
    CHECK(!session.canUndo() && session.canRedo());
    for (size_t i = 1; i < states.size(); ++i)
    {
        run(session, SceneOperation::Redo);
        CHECK(pipelineSnapshot(storage, session) == states[i]);
    }
    CHECK(!session.canRedo());
    run(session, SceneOperation::Save); CHECK(!session.dirty());
    run(session, SceneOperation::Undo); CHECK(session.dirty());
    run(session, SceneOperation::Redo); CHECK(!session.dirty());
    const auto authored = pipelineSnapshot(storage, session);
    for (int cycle = 0; cycle < 3; ++cycle)
    {
        run(session, SceneOperation::Play);
        CHECK(session.activeContext().instance != session.editorContext().instance);
        for (auto [id, transform] : session.activeContext().scene.view<Transform>())
            transform.position = math::Vector3{ 99, 98, 97 };
        (void)session.activeContext().scene.createEntity("simulation only");
        run(session, SceneOperation::Stop);
        CHECK(pipelineSnapshot(storage, session) == authored && !session.dirty());
    }
    run(session, SceneOperation::Open, "pipeline");
    CHECK(pipelineSnapshot(storage, session) == authored);
    CHECK(!session.canUndo() && !session.canRedo());
    CHECK(storage.files.at("pipeline") == authored);
}

static void pipelineBranchFailures()
{
    for (auto operation : { SceneOperation::DuplicateEntity, SceneOperation::DeleteEntity })
        for (int phase = 0; phase < 5; ++phase)
        {
            MemoryStorage storage;
            SceneSession session(storage);
            run(session, SceneOperation::CreateEntity);
            const auto key = session.keyFor(session.takeCreatedEntity());
            run(session, SceneOperation::SaveAs, "atomic");
            pipelineAction(session, SceneOperation::DuplicateEntity, key);
            run(session, SceneOperation::Undo); // keep a redo branch to detect accidental truncation
            const auto baseline = pipelineSnapshot(storage, session);
            const auto file = storage.files.at("atomic");
            const auto instance = session.editorContext().instance;
            if (phase < 2) storage.failSnapshotAt = storage.snapshots + phase + 1;
            else storage.failRestoreAt = storage.restores + phase - 1;
            SceneRequest request{ operation, session.documentId() }; request.entity = key;
            session.enqueue(request);
            session.enqueue(SceneOperation::CreateEntity); // must be discarded after failure
            session.processRequests();
            CHECK(!session.error().empty());
            CHECK(pipelineSnapshot(storage, session) == baseline);
            CHECK(session.editorContext().instance == instance);
            CHECK(session.canRedo() && session.canUndo() && !session.dirty());
            CHECK(session.path() == "atomic" && storage.files.at("atomic") == file);
            storage.failSnapshotAt = storage.failRestoreAt = 0;
            run(session, SceneOperation::Redo);
            CHECK(session.editorScene().entities().size() == 3);
        }
}

static void pipelineInvalidRequests()
{
    MemoryStorage storage;
    SceneSession session(storage);
    run(session, SceneOperation::CreateEntity);
    const auto key = session.keyFor(session.takeCreatedEntity());
    run(session, SceneOperation::SaveAs, "guarded");
    const auto baseline = pipelineSnapshot(storage, session);
    const auto family = reflection::ComponentFamily::getID<physics::RigidBody>();
    for (auto operation : { SceneOperation::DuplicateEntity, SceneOperation::DeleteEntity,
        SceneOperation::AddComponent, SceneOperation::RemoveComponent })
    {
        for (EntityKey invalid : { EntityKey(0), EntityKey(999999) })
        {
            SceneRequest request{ operation, session.documentId() }; request.entity = invalid; request.component = family;
            session.enqueue(request); session.processRequests();
            CHECK(!session.error().empty()); CHECK(pipelineSnapshot(storage, session) == baseline);
            CHECK(!session.dirty());
        }
    }
    run(session, SceneOperation::Play);
    for (auto operation : { SceneOperation::CreateEntity, SceneOperation::DuplicateEntity, SceneOperation::DeleteEntity,
        SceneOperation::AddComponent, SceneOperation::RemoveComponent, SceneOperation::Undo, SceneOperation::Redo, SceneOperation::Import })
    {
        SceneRequest request{ operation, session.documentId() }; request.entity = key; request.component = family;
        session.enqueue(request); session.processRequests(); CHECK(!session.error().empty());
        CHECK(session.playing() && pipelineSnapshot(storage, session) == baseline);
    }
    run(session, SceneOperation::Stop);
    SceneRequest stale{ SceneOperation::DeleteEntity, session.documentId() }; stale.entity = key;
    run(session, SceneOperation::Open, "guarded");
    session.enqueue(stale); session.processRequests();
    CHECK(!session.error().empty() && pipelineSnapshot(storage, session) == baseline);
    CHECK(!session.canUndo() && !session.dirty());
}

static void pipelineDiskRoundtrip()
{
    const auto file = std::filesystem::temp_directory_path() / (L"CSyren-pipeline-" + std::to_wstring(GetCurrentProcessId()) + L".scene");
    struct Cleanup { std::filesystem::path file; ~Cleanup() { std::error_code ignored; std::filesystem::remove(file, ignored); } } cleanup{ file };
    Serializer serializer;
    ArchiveSceneStorage storage(serializer);
    SceneSession session(storage);
    run(session, SceneOperation::CreateEntity);
    const auto key = session.keyFor(session.takeCreatedEntity());
    position(session, key, math::Vector3{ 7, 8, 9 });
    pipelineAction(session, SceneOperation::AddComponent, key, reflection::ComponentFamily::getID<physics::BoxCollider>());
    pipelineAction(session, SceneOperation::DuplicateEntity, key);
    const auto copy = session.keyFor(session.takeCreatedEntity());
    pipelineAction(session, SceneOperation::DeleteEntity, copy);
    run(session, SceneOperation::Undo);
    const auto expected = pipelineSnapshot(storage, session);
    run(session, SceneOperation::SaveAs, core::pathToUtf8(file));
    SceneSession reopened(storage); // fresh session: no shared history or entity map
    run(reopened, SceneOperation::Open, core::pathToUtf8(file));
    CHECK(pipelineSnapshot(storage, reopened) == expected);
    CHECK(reopened.editorScene().entities().size() == 3);
    CHECK(!reopened.dirty() && !reopened.canUndo() && !reopened.canRedo());
}

static void pipelineNestedReferences()
{
    MemoryStorage storage;
    SceneSession session(storage);
    auto& scene = session.editorScene();
    const auto container = scene.createEntity("container");
    const auto root = scene.createEntity("root", container);
    const auto child = scene.createEntity("child", root);
    const auto grandchild = scene.createEntity("grandchild", child);
    const auto sibling = scene.createEntity("sibling", root);
    const auto external = scene.createEntity("external");
    scene.addComponent<Link>(root)->target = grandchild;
    scene.addComponent<Link>(grandchild)->target = root;
    scene.addComponent<Link>(child)->target = sibling;
    scene.addComponent<Link>(sibling)->target = external;
    const auto rootKey = session.keyFor(root), containerKey = session.keyFor(container), externalKey = session.keyFor(external);
    run(session, SceneOperation::SaveAs, "nested");
    const auto baseline = pipelineSnapshot(storage, session);
    pipelineAction(session, SceneOperation::DuplicateEntity, rootKey);
    const auto copy = session.takeCreatedEntity();
    const auto copyKey = session.keyFor(copy);
    auto& copiedScene = session.editorScene();
    CHECK(copiedScene.entities().try_get(copy)->parent == session.resolve(containerKey));
    const auto copyGrandchild = copiedScene.getComponent<Link>(copy)->target;
    const auto copyChild = copiedScene.entities().try_get(copyGrandchild)->parent;
    const auto copySibling = copiedScene.getComponent<Link>(copyChild)->target;
    CHECK(copiedScene.entities().try_get(copyChild)->parent == copy);
    CHECK(copiedScene.entities().try_get(copySibling)->parent == copy);
    CHECK(copiedScene.getComponent<Link>(copyGrandchild)->target == copy);
    CHECK(copiedScene.getComponent<Link>(copySibling)->target == session.resolve(externalKey));
    CHECK(copiedScene.getComponent<Link>(session.resolve(rootKey))->target != copyGrandchild);
    const auto duplicated = pipelineSnapshot(storage, session);
    pipelineAction(session, SceneOperation::DeleteEntity, copyKey);
    CHECK(pipelineSnapshot(storage, session) == baseline);
    run(session, SceneOperation::Undo);
    CHECK(pipelineSnapshot(storage, session) == duplicated);
    run(session, SceneOperation::Save);
    run(session, SceneOperation::Open, "nested");
    CHECK(pipelineSnapshot(storage, session) == duplicated);
}

static void pipelinePendingEdits()
{
    for (const auto operation : { SceneOperation::DuplicateEntity, SceneOperation::DeleteEntity })
    {
        MemoryStorage storage;
        SceneSession session(storage);
        run(session, SceneOperation::CreateEntity);
        const auto key = session.keyFor(session.takeCreatedEntity());
        run(session, SceneOperation::SaveAs, "pending");
        const auto initial = pipelineSnapshot(storage, session);
        position(session, key, math::Vector3{ 1, 2, 3 }, false);
        position(session, key, math::Vector3{ 4, 5, 6 }, false);
        const auto edited = pipelineSnapshot(storage, session);
        pipelineAction(session, operation, key); // finishes the drag before its own command
        const auto changed = pipelineSnapshot(storage, session);
        run(session, SceneOperation::Undo); CHECK(pipelineSnapshot(storage, session) == edited);
        run(session, SceneOperation::Undo); CHECK(pipelineSnapshot(storage, session) == initial && !session.dirty());
        run(session, SceneOperation::Redo); CHECK(pipelineSnapshot(storage, session) == edited);
        run(session, SceneOperation::Redo); CHECK(pipelineSnapshot(storage, session) == changed);
        run(session, SceneOperation::Undo);
        position(session, key, math::Vector3{ 7, 8, 9 });
        CHECK(!session.canRedo() && session.dirty());
        run(session, SceneOperation::Undo); CHECK(pipelineSnapshot(storage, session) == edited);
    }
}
