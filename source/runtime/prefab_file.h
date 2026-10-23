#pragma once
// Prefab files and the entity-branch archive they share with the editor.
//
// A .prefab file is a scene document (the same JSON as a .scene, so it can also
// be imported as a scene) holding one branch: exactly one entity without a
// parent (the root) and its descendants. Paths are relative to the project's
// assets ("prefabs/grenade.prefab").
//
// BranchArchive is the archived form of a branch: names, parents, component
// values (JSON, unknown component types included) and the serialization
// sections (the resource table). The editor
// uses it for Duplicate and "Save as prefab", PrefabFile to create copies.
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "core/prefab.h"
#include "core/serialization_context.h"
#include "core/scene_serialization_context.h"
#include "third_party/json/json.hpp"

namespace csyren::core::reflection { struct ComponentMeta; }
namespace csyren::core { class EntityBatch; }

namespace csyren
{
    // Attaches the serialization sections (e.g. the resource table) to a context.
    using ConfigureArchive = std::function<void(core::SerializationContext&)>;

    struct BranchArchive
    {
        static constexpr size_t npos = static_cast<size_t>(-1);
        struct Component { const core::reflection::ComponentMeta* meta = nullptr; nlohmann::json value; };
        // A component of a type this program does not know (core::UnknownComponents).
        struct UnknownComponent { std::string name; nlohmann::json value; };
        struct Entity
        {
            std::string name;
            size_t parent = npos;   // index in `entities`; npos for the root
            uint64_t id = 0;        // id in SceneEntityReferences (saved / loaded)
            std::vector<Component> components;
            std::vector<UnknownComponent> unknown; // kept as data and written back
        };
        std::vector<Entity> entities;                   // entities[0] is the root; parents come first
        nlohmann::json sections = nlohmann::json::object();

        // The root and its descendants, root first, then breadth-first.
        static std::vector<core::Entity::ID> collect(core::Scene& scene, core::Entity::ID root);

        // Archives `branch` (from collect()). Entity references are written through
        // the context's SceneEntityReferences::saved, filled by the caller; an
        // entity's id in the archive is its `saved` value.
        bool save(core::Scene& scene, const std::vector<core::Entity::ID>& branch,
            core::SerializationContext& context, std::string& error);
        // Reads `sections` into the context (resource table): once per context.
        bool loadSections(core::SerializationContext& context, std::string& error) const;
        // Creates the entities in `batch` (same order as `entities`) and maps
        // references.loaded[entity.id] to them, so references inside the branch
        // resolve to the copies.
        std::vector<core::Entity::ID> create(core::EntityBatch& batch, core::SceneEntityReferences& references) const;
        // Parents and component values of the entities from create(). Unknown
        // components go to core::UnknownComponents (dropped if it is not registered).
        bool load(core::EntityBatch& batch, const std::vector<core::Entity::ID>& ids,
            core::SerializationContext& context, std::string& error) const;

        // Scene document form ({"$version":2,"entities":[...],<sections>}).
        nlohmann::json document() const;
        // Reads a document with exactly one root. Components of unknown types are
        // kept in Entity::unknown and their names listed in `unknownNames`.
        bool fromDocument(const nlohmann::json& document, std::string& error, std::vector<std::string>* unknownNames = nullptr);
    };

    // A prefab read from a file. The document is parsed once; each copy reads the
    // component values from it, resolving resources through a context that is
    // kept between copies (resources are looked up once, not per spawn).
    class PrefabFile final : public core::Prefab
    {
    public:
        PrefabFile(std::string path, BranchArchive archive, ConfigureArchive configure);
        const std::string& path() const noexcept override { return _path; }
        core::Entity::ID instantiate(core::Scene& scene, const core::SpawnPose& pose,
            const std::vector<core::PrefabEdit>& edits = {}, core::Entity::ID root = core::Entity::invalidID) const override;
        size_t entityCount() const noexcept { return _archive.entities.size(); }
        std::vector<std::string> references() const override { return _references; }
        // Reads the sections and every component value once (validation, resources
        // resolved up front); false with `error` when the prefab cannot be loaded.
        bool prepare(std::string& error);
    private:
        bool tryInstantiate(core::Scene& scene, const core::SpawnPose& pose,
            const std::vector<core::PrefabEdit>& edits, core::Entity::ID reservedRoot, core::Entity::ID& root, std::string& error) const;
        std::string _path;
        BranchArchive _archive;
        ConfigureArchive _configure;
        std::vector<std::string> _references; // PrefabRef paths found by prepare()
        mutable std::mutex _mutex;
        mutable core::SerializationContext _context;
    };

    // Reads `path` (relative to the assets) into a prefab; null and `error` on failure.
    std::shared_ptr<const PrefabFile> loadPrefabFile(const std::string& path, const ConfigureArchive& configure, std::string& error);
    // Loader for core::PrefabLibrary.
    core::PrefabLibrary::Loader prefabFileLoader(ConfigureArchive configure);
    // Writes the branch under `root` as a prefab file. References from the branch
    // to entities outside it cannot be saved (error).
    bool savePrefabFile(core::Scene& scene, core::Entity::ID root, const std::string& path,
        const ConfigureArchive& configure, std::string& error);
}
