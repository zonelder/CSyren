#include <algorithm>
#include "core/scene.h"
#include "core/meta_archive.h"
#include "core/meta_json.h"
#include "core/scene_serialization_context.h"
#include "../../serializer.h"
#include <cassert>
#include <iostream>

struct Persisted {
    int value = 5;
    static void describe() {
        using namespace csyren::core::reflection;
        MetaFactory<int>{}.primitiveType("int"_hs).archive(scalarArchive<int>());
        MetaFactory<Persisted>{}.type("Persisted"_hs).data<&Persisted::value>("value"_hs);
    }
};
struct Link {
    csyren::core::Entity::ID target{};
    static void describe() {
        using namespace csyren::core;
        using namespace csyren::core::reflection;
        MetaFactory<Link>{}.primitiveType("Link"_hs).archive({
            [](ArchiveWriter& w, SerializationContext& c, const MetaType&, const void* p) {
                auto* refs = c.tryGet<SceneEntityReferences>();
                if (!refs) return false;
                auto it = refs->saved.find(static_cast<const Link*>(p)->target);
                return it != refs->saved.end() && w.unsignedInteger(it->second);
            },
            [](ArchiveReader& r, SerializationContext& c, const MetaType&, void* p) {
                uint64_t local{};
                auto* refs = c.tryGet<SceneEntityReferences>();
                if (!refs || !r.unsignedInteger(local)) return false;
                auto it = refs->loaded.find(local);
                if (it == refs->loaded.end()) return false;
                static_cast<Link*>(p)->target = it->second;
                return true;
            }
        });
    }
};
struct MoveOnly {
    std::unique_ptr<int> payload = std::make_unique<int>(123);
    static inline int moves{};
    MoveOnly() = default;
    MoveOnly(MoveOnly&& other) noexcept : payload(std::move(other.payload)) { ++moves; }
    MoveOnly& operator=(MoveOnly&&) noexcept = default;
    static void describe() { csyren::core::reflection::MetaFactory<MoveOnly>{}.type("MoveOnly"_hs); }
};
struct TestSection : csyren::core::SerializationSection {
    int value{};
    std::function<bool(csyren::core::SerializationContext&)> beforeAppend;
    uint64_t id() const override { return "extension"_hs.id; }
    std::string_view name() const override { return "extension"; }
    void reset() override { value = 77; }
    bool save(csyren::core::ArchiveWriter& w, csyren::core::SerializationContext&) override { return w.signedInteger(value); }
    bool load(csyren::core::ArchiveReader& r, csyren::core::SerializationContext&) override { int64_t v{}; if (!r.signedInteger(v)) return false; value = static_cast<int>(v); return true; }
    bool finishLoad(csyren::core::SerializationContext& c) override { return !beforeAppend || beforeAppend(c); }
};
int main() {
    using namespace csyren::core;
    REGISTER_COMPONENT(Persisted);
    REGISTER_COMPONENT(Link);
    REGISTER_COMPONENT(MoveOnly);
    auto bus = std::make_unique<EventBus2>(); Scene source(bus.get()); source.init();
    auto id = source.createEntity("example"); source.addComponent<Persisted>(id)->value = 42;
    auto child = source.createEntity("child", id); source.addComponent<Persisted>(child)->value = 99;
    source.addComponent<Link>(id)->target = child;
    csyren::Serializer serializer;
    SerializationContext context; context.attachSection<TestSection>();
    nlohmann::json document;
    JsonArchiveWriter writer(document);
    assert(serializer.saveScene(writer, context, source));
    assert(document["$version"] == 2);
    for (const auto& entity : document["entities"]) {
        assert(!entity.contains("$version"));
        assert(!entity["components"].contains("$version"));
        assert(!entity["components"]["Persisted"].contains("$version"));
    }
    assert(document["entities"].size() == 2);
    assert(document["entities"][0]["id"] == 0);
    assert(document["entities"][1]["parent"] == 0);
    assert(document["extension"] == 77);
    document["extension"] = 88;
    document["entities"][0]["components"]["Unknown"] = { { "x", 1 } };
    auto destinationBus = std::make_unique<EventBus2>(); Scene destination(destinationBus.get()); destination.init();
    const auto existing = destination.createEntity("existing");
    destination.addComponent<Persisted>(existing)->value = 17;
    int linkEvents = 0;
    destinationBus->subscribe<ComponentCreateEvent<Link>>([&](auto& event) {
        assert(event.comp);
        assert(destination.hasComponent<Persisted>(event.comp->target));
        ++linkEvents;
    });
    JsonArchiveReader reader(document);
    assert(serializer.loadScene(reader, context, destination));
    assert(context.tryGet<TestSection>()->value == 88);
    assert(destination.entities().size() == 4);
    assert(destination.getComponent<Persisted>(existing)->value == 17);
    assert(destination.getComponent<Link>(context.tryGet<SceneEntityReferences>()->loaded.at(0))->target == context.tryGet<SceneEntityReferences>()->loaded.at(1));
    assert(!context.tryGet<EntityBatch>());
    destinationBus->commit_batch(); assert(linkEvents == 1);
    auto* mapping = context.tryGet<SceneEntityReferences>();
    assert(mapping && mapping->loaded.at(0) != id);
    for (const auto& entity : destination.entities())
        if (entity.name == "child") assert(entity.parent == mapping->loaded.at(0));
    bool found = false;
    for (const auto& entity : destination.entities()) {
        if (entity.name != "example") continue;
        found = true;
        const auto* info = reflection::ComponentRegistry::get("Persisted");
        assert(static_cast<Persisted*>(info->getRaw(&destination, entity.id))->value == 42);
    }
    assert(found);
    // Forward parents and repeated imports never merge into existing entities.
    auto forward = document;
    std::swap(forward["entities"][0], forward["entities"][1]);
    JsonArchiveReader forwardReader(forward);
    assert(serializer.loadScene(forwardReader, context, destination));
    assert(destination.entities().size() == 6);
    assert(destination.getComponent<Persisted>(existing)->value == 17);
    // Duplicate document identities are rejected without retaining mappings.
    auto duplicate = document;
    duplicate["entities"][1]["id"] = duplicate["entities"][0]["id"];
    JsonArchiveReader duplicateReader(duplicate);
    assert(!serializer.loadScene(duplicateReader, context, destination));
    assert(destination.entities().size() == 6);
    assert(context.tryGet<SceneEntityReferences>()->loaded.empty());
    // finishLoad observes isolated staging; rejecting it never changes live pools.
    auto oldValueAddress = destination.getComponent<Persisted>(existing).get();
    context.tryGet<TestSection>()->beforeAppend = [&](SerializationContext& c) {
        auto* imported = c.tryGet<EntityBatch>();
        assert(imported && imported->size() == 2);
        assert(destination.entities().size() == 6);
        auto newID = c.tryGet<SceneEntityReferences>()->loaded.at(0);
        assert(imported->getComponent(newID, reflection::ComponentFamily::getID<Persisted>()));
        return false;
    };
    JsonArchiveReader rejected(document);
    assert(!serializer.loadScene(rejected, context, destination));
    assert(destination.entities().size() == 6);
    assert(destination.getComponent<Persisted>(existing).get() == oldValueAddress);
    assert(!context.tryGet<EntityBatch>());
    context.tryGet<TestSection>()->beforeAppend = {};
    // Legacy scenes have no archive version fields and no extension sections.
    document.erase("$version"); document.erase("extension");
    size_t persistedEntity = 0;
    for (size_t i = 0; i < document["entities"].size(); ++i) {
        auto& entity = document["entities"][i];
        entity["id"] = i + 1; entity.erase("parent");
        entity.erase("$version"); entity["components"].erase("$version");
        if (entity["components"].contains("Persisted")) {
            persistedEntity = i; entity["components"]["Persisted"].erase("$version");
        }
    }
    auto legacyBus = std::make_unique<EventBus2>(); Scene legacy(legacyBus.get()); legacy.init();
    JsonArchiveReader legacyReader(document);
    assert(serializer.loadScene(legacyReader, context, legacy));
    auto withRoot = document;
    withRoot["entities"].push_back({ {"id", 0}, {"name", "legacy root"}, {"components", nlohmann::json::object()} });
    JsonArchiveReader rootReader(withRoot);
    const auto beforeRootImport = legacy.entities().size();
    assert(serializer.loadScene(rootReader, context, legacy));
    assert(legacy.entities().size() == beforeRootImport + 2);
    document["entities"][persistedEntity]["components"]["Persisted"]["value"] = "invalid";
    JsonArchiveReader badReader(document);
    const auto previousCount = legacy.entities().size();
    legacyBus->commit_batch();
    int published = 0;
    legacyBus->subscribe<EntityCreateEvent>([&](auto&) { ++published; });
    assert(!serializer.loadScene(badReader, context, legacy));
    assert(legacy.entities().size() == previousCount);
    // A new family adopts the typed pool, preserving the component address.
    auto first = destination.stageEntities(1);
    const auto firstID = first.createEntity("move-only");
    auto* staged = first.addComponent<MoveOnly>(firstID);
    assert(first.getComponent<MoveOnly>(firstID) == staged);
    MoveOnly::moves = 0;
    assert(destination.append(std::move(first)));
    assert(destination.getComponent<MoveOnly>(firstID).get() == staged);
    assert(MoveOnly::moves == 0);
    assert(!destination.append(std::move(first)));
    auto second = destination.stageEntities(1);
    const auto secondID = second.createEntity("move-only second");
    second.addComponent(secondID, reflection::ComponentFamily::getID<MoveOnly>());
    auto moved = std::move(second);
    assert(!destination.append(std::move(second)));
    assert(destination.append(std::move(moved)));
    assert(*destination.getComponent<MoveOnly>(firstID)->payload == 123);
    assert(*destination.getComponent<MoveOnly>(secondID)->payload == 123);
    // Stale ID plans are rejected, rather than silently remapping decoded references.
    auto stale = destination.stageEntities(1); stale.createEntity("stale");
    const auto intervening = destination.createEntity("intervening");
    const auto stableCount = destination.entities().size();
    assert(!destination.append(std::move(stale)));
    assert(destination.entities().size() == stableCount);
    // Recycled IDs are planned and committed in the allocator's original order.
    destination.destroyEntity(firstID); destination.flush();
    auto recycled = destination.stageEntities(1);
    const auto recycledID = recycled.createEntity("recycled");
    assert(recycledID == firstID && destination.append(std::move(recycled)));
    assert(destination.entities().size() == stableCount);
    (void)intervening;
    legacyBus->commit_batch(); assert(published == 0);
    // Invalid hierarchy is rejected before touching the live scene.
    document["entities"][persistedEntity]["components"]["Persisted"]["value"] = 42;
    document["$version"] = 2;
    document["entities"][0]["parent"] = document["entities"][0]["id"];
    JsonArchiveReader cycle(document);
    assert(!serializer.loadScene(cycle, context, legacy));
    assert(legacy.entities().size() == previousCount);
    std::cout << "scene archive checks passed\n";
}
