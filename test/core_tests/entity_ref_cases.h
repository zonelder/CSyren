#pragma once
// EntityRef fields (core/entity_ref.h): saved as document numbers and remapped on
// load, Duplicate and prefab copies.
// Included by scene_workflow_tests.cpp after prefab_cases.h: uses CHECK, PrefabProject
// and prefabEntityNamed().
#include "core/entity_ref.h"
#include "editor_commands.h"

struct RefHolder
{
    EntityRef target;
    EntityRef other;
    static void describe()
    {
        core::reflection::MetaFactory<RefHolder>{}.type("RefHolder"_hs)
            .data<&RefHolder::target>("target"_hs).data<&RefHolder::other>("other"_hs);
    }
};
static void registerEntityRefTestTypes()
{
    static const bool registered = [] {
        EntityRef::describe();
        REGISTER_COMPONENT(RefHolder);
        return true;
    }();
    (void)registered;
}

static nlohmann::json saveToJson(Scene& scene)
{
    nlohmann::json document;
    JsonArchiveWriter writer(document); SerializationContext context;
    CHECK(Serializer{}.saveScene(writer, context, scene));
    return document;
}
static const nlohmann::json& savedEntity(const nlohmann::json& document, const std::string& name)
{
    for (const auto& entity : document.at("entities")) if (entity.at("name") == name) return entity;
    throw std::runtime_error("no entity " + name);
}

static void entityRefSceneRoundtrip()
{
    registerEntityRefTestTypes();
    SceneInstance source;
    auto& scene = source.scene();
    const auto a = scene.createEntity("a");
    const auto b = scene.createEntity("b");
    const auto gone = scene.createEntity("gone");
    scene.addComponent<RefHolder>(a, RefHolder{ { b }, {} });
    scene.addComponent<RefHolder>(b, RefHolder{ { gone }, { a } });
    scene.destroyEntity(gone);
    scene.flush();

    const auto document = saveToJson(scene);
    const auto& savedA = savedEntity(document, "a");
    const auto& savedB = savedEntity(document, "b");
    CHECK(savedA.at("components").at("RefHolder").at("target") == savedB.at("id"));
    CHECK(savedA.at("components").at("RefHolder").at("other") == -1);  // empty
    CHECK(savedB.at("components").at("RefHolder").at("target") == -1); // destroyed entity
    CHECK(savedB.at("components").at("RefHolder").at("other") == savedA.at("id"));

    SceneInstance loaded;
    {
        JsonArchiveReader reader(document); SerializationContext context;
        CHECK(Serializer{}.loadScene(reader, context, loaded.scene()));
    }
    const auto loadedA = prefabEntityNamed(loaded.scene(), "a");
    const auto loadedB = prefabEntityNamed(loaded.scene(), "b");
    CHECK(loaded.scene().getComponent<RefHolder>(loadedA)->target == EntityRef{ loadedB });
    CHECK(loaded.scene().getComponent<RefHolder>(loadedA)->other.empty());
    CHECK(loaded.scene().getComponent<RefHolder>(loadedB)->target.empty());
    CHECK(loaded.scene().getComponent<RefHolder>(loadedB)->other == EntityRef{ loadedA });
}

static void entityRefDuplicateAndPrefab()
{
    registerEntityRefTestTypes();
    PrefabProject project;
    SceneInstance source;
    auto& scene = source.scene();
    const auto outside = scene.createEntity("outside");
    const auto crate = scene.createEntity("crate");
    const auto lid = scene.createEntity("lid", crate);
    scene.addComponent<Transform>(crate);
    scene.addComponent<RefHolder>(crate, RefHolder{ { lid }, { outside } });

    // Duplicate (as SceneSession does): every entity is in `saved`, references
    // that leave the branch load back as the originals.
    const auto branch = BranchArchive::collect(scene, crate);
    SerializationContext saving;
    auto& savedRefs = saving.emplaceService<SceneEntityReferences>();
    uint64_t number = 0;
    for (const auto& entity : scene.entities())
        if (entity.id != EntityManager::ROOT_PARENT) savedRefs.saved.emplace(entity.id, number++);
    BranchArchive archive;
    std::string error;
    CHECK(archive.save(scene, branch, saving, error));
    SerializationContext loading;
    auto& loadedRefs = loading.emplaceService<SceneEntityReferences>();
    for (const auto& [id, saved] : savedRefs.saved) loadedRefs.loaded.emplace(saved, id);
    CHECK(archive.loadSections(loading, error));
    auto batch = scene.stageEntities(branch.size());
    std::vector<Entity::ID> copies;
    {
        auto scope = loading.scopedService(batch);
        copies = archive.create(batch, loadedRefs);
        CHECK(archive.load(batch, copies, loading, error) && loading.finishLoad());
    }
    CHECK(scene.append(std::move(batch)));
    CHECK(scene.getComponent<RefHolder>(copies[0])->target == EntityRef{ copies[1] }); // inside: the copy
    CHECK(scene.getComponent<RefHolder>(copies[0])->other == EntityRef{ outside });    // leaving: the original

    // A prefab cannot refer to an entity outside its branch...
    CHECK(!savePrefabFile(scene, crate, "prefabs/refs.prefab", {}, error));
    CHECK(error.find("RefHolder") != std::string::npos);
    // ...a reference to a destroyed one is saved as none.
    scene.destroyEntity(outside);
    scene.flush();
    CHECK(savePrefabFile(scene, crate, "prefabs/refs.prefab", {}, error));
    core::PrefabLibrary library;
    library.setLoader(prefabFileLoader({}));
    const auto prefab = library.load("prefabs/refs.prefab");
    CHECK(prefab);
    SceneInstance target;
    const auto root = prefab->instantiate(target.scene(), {});
    CHECK(root != Entity::invalidID);
    const auto* copy = target.scene().entities().try_get(root);
    CHECK(copy && copy->children.size() == 1);
    CHECK(target.scene().getComponent<RefHolder>(root)->target == EntityRef{ copy->children.front() });
    CHECK(target.scene().getComponent<RefHolder>(root)->other.empty());
}

// The inspector's edit keeps editor keys: undo and redo work after the scene is
// rebuilt with new runtime IDs (SceneSession does that on structural changes).
static void entityRefFieldEditUsesKeys()
{
    registerEntityRefTestTypes();
    const auto family = core::reflection::ComponentFamily::getID<RefHolder>();
    const editor::FieldPath path{ "target"_hs.id };
    SceneInstance first;
    const auto holder = first.scene().createEntity("holder");
    const auto target = first.scene().createEntity("target");
    first.scene().addComponent<RefHolder>(holder);
    editor::EntityMap keys{ { 1, holder }, { 2, target } };
    editor::EditContext context{ first.scene(), keys };
    editor::EntityRefFieldEdit edit(1, family, path, 0, 2);
    CHECK(edit.changed());
    CHECK(edit.execute(context));
    CHECK(first.scene().getComponent<RefHolder>(holder)->target == EntityRef{ target });

    SceneInstance rebuilt;
    (void)rebuilt.scene().createEntity("shifts the IDs");
    const auto holder2 = rebuilt.scene().createEntity("holder");
    const auto target2 = rebuilt.scene().createEntity("target");
    rebuilt.scene().addComponent<RefHolder>(holder2, RefHolder{ { target2 }, {} });
    editor::EntityMap rebuiltKeys{ { 1, holder2 }, { 2, target2 } };
    editor::EditContext rebuiltContext{ rebuilt.scene(), rebuiltKeys };
    CHECK(edit.undo(rebuiltContext));
    CHECK(rebuilt.scene().getComponent<RefHolder>(holder2)->target.empty());
    CHECK(edit.execute(rebuiltContext));
    CHECK(rebuilt.scene().getComponent<RefHolder>(holder2)->target == EntityRef{ target2 });
}
