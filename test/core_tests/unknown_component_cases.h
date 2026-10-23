#pragma once
// Components of unknown types (core/unknown_components.h): kept on load, written
// back on save, carried by duplicates and prefab documents.
// Included by scene_workflow_tests.cpp after prefab_cases.h: uses CHECK and prefabEntityNamed().
#include "core/unknown_components.h"

static const char* unknownComponentsDocument()
{
    return R"({"$version":2,"entities":[
        {"id":0,"name":"root","components":{
            "Transform":{"position":[1,2,3],"rotation":[0,0,0,1],"scale":[1,1,1]},
            "FutureComponent":{"speed":2.5,"tags":["x",1,-2,true,null],"nested":{"big":18446744073709551615,"empty":{}}},
            "RemovedScalar":3}},
        {"id":1,"parent":0,"name":"child","components":{"RemovedList":[]}}]})";
}

static const UnknownComponents* unknownComponentsOf(Scene& scene, Entity::ID id)
{
    return static_cast<const UnknownComponents*>(scene.getComponentRaw(id, reflection::ComponentFamily::getID<UnknownComponents>()));
}

// The kept value of `name` as JSON (null when there is none).
static nlohmann::json keptUnknownValue(Scene& scene, Entity::ID id, std::string_view name)
{
    const auto* unknown = unknownComponentsOf(scene, id);
    const auto* entry = unknown ? unknown->find(name) : nullptr;
    nlohmann::json value;
    if (entry)
    {
        JsonArchiveWriter writer(value);
        CHECK(core::write(writer, entry->value));
    }
    return value;
}

static const nlohmann::json& savedComponents(const nlohmann::json& document, const std::string& entityName)
{
    for (const auto& entity : document.at("entities"))
        if (entity.at("name") == entityName) return entity.at("components");
    throw std::runtime_error("no entity " + entityName);
}

static void unknownComponentsRoundtrip()
{
    const auto source = nlohmann::json::parse(unknownComponentsDocument());
    const auto& sourceRoot = source["entities"][0]["components"];
    SceneInstance loaded;
    auto& scene = loaded.scene();
    {
        JsonArchiveReader reader(source); SerializationContext context;
        CHECK(Serializer{}.loadScene(reader, context, scene));
    }
    const auto root = prefabEntityNamed(scene, "root");
    const auto child = prefabEntityNamed(scene, "child");
    CHECK(scene.getComponent<Transform>(root)->position.y == 2.0f); // known components load as usual
    const auto* unknown = unknownComponentsOf(scene, root);
    CHECK(unknown && unknown->entries.size() == 2);
    CHECK(keptUnknownValue(scene, root, "FutureComponent") == sourceRoot["FutureComponent"]);
    CHECK(keptUnknownValue(scene, root, "RemovedScalar") == 3);
    CHECK(keptUnknownValue(scene, child, "RemovedList") == nlohmann::json::array());

    // Saved back unchanged, next to the known components.
    nlohmann::json saved;
    {
        JsonArchiveWriter writer(saved); SerializationContext context;
        CHECK(Serializer{}.saveScene(writer, context, scene));
    }
    CHECK(savedComponents(saved, "root").at("FutureComponent") == sourceRoot["FutureComponent"]);
    CHECK(savedComponents(saved, "root").at("RemovedScalar") == 3);
    CHECK(savedComponents(saved, "root").contains("Transform"));
    CHECK(!savedComponents(saved, "root").contains("UnknownComponents"));
    CHECK(savedComponents(saved, "child").at("RemovedList") == nlohmann::json::array());

    // Duplicate (the editor's branch archive) carries them to the copies.
    const auto branch = BranchArchive::collect(scene, root);
    SerializationContext saving;
    auto& savedRefs = saving.emplaceService<SceneEntityReferences>();
    for (size_t i = 0; i < branch.size(); ++i) savedRefs.saved.emplace(branch[i], i);
    BranchArchive archive;
    std::string error;
    CHECK(archive.save(scene, branch, saving, error));
    SerializationContext loading;
    auto& loadedRefs = loading.emplaceService<SceneEntityReferences>();
    CHECK(archive.loadSections(loading, error));
    auto batch = scene.stageEntities(branch.size());
    std::vector<Entity::ID> copies;
    {
        auto scope = loading.scopedService(batch);
        copies = archive.create(batch, loadedRefs);
        CHECK(archive.load(batch, copies, loading, error) && loading.finishLoad());
    }
    CHECK(scene.append(std::move(batch)));
    CHECK(keptUnknownValue(scene, copies[0], "FutureComponent") == sourceRoot["FutureComponent"]);
    CHECK(keptUnknownValue(scene, copies[1], "RemovedList") == nlohmann::json::array());

    // Prefab documents keep them as well.
    const auto document = archive.document();
    CHECK(document["entities"][0]["components"]["FutureComponent"] == sourceRoot["FutureComponent"]);
    BranchArchive reread;
    std::vector<std::string> names;
    CHECK(reread.fromDocument(document, error, &names));
    std::sort(names.begin(), names.end());
    CHECK((names == std::vector<std::string>{ "FutureComponent", "RemovedList", "RemovedScalar" }));
    CHECK(reread.document()["entities"][0]["components"]["RemovedScalar"] == 3);
}
