#pragma once
#include "resource_handle.h"
#include "core/meta.h"
#include "core/registration.h"
#include <optional>
#include <unordered_map>
#include <vector>
#include <string>

namespace csyren::render
{
    class ResourceManager;
    struct ResourceRecord { uint64_t id{}; std::string type; std::string path; };
    struct ResourceKey { uint64_t type; uint64_t runtimeID; uint64_t generation; bool operator==(const ResourceKey&) const = default; };
    struct ResourceKeyHash { size_t operator()(ResourceKey key) const noexcept { return std::hash<uint64_t>{}(key.type) ^ (std::hash<uint64_t>{}(key.runtimeID) << 1) ^ (std::hash<uint64_t>{}(key.generation) << 2); } };
    struct ResourceIdentity { uint64_t id; uint64_t generation; };
    class ResourceSerializationContext : public core::SerializationSection
    {
    public:
        explicit ResourceSerializationContext(ResourceManager& manager) : _manager(manager) {}
        std::vector<ResourceRecord> records;
        std::unordered_map<ResourceKey, uint64_t, ResourceKeyHash> saved;
        std::unordered_map<uint64_t, ResourceIdentity> loaded;
        std::unordered_map<uint64_t, size_t> recordIndex;
        ResourceManager& manager() const noexcept { return _manager; }
        bool indexRecords();
        uint64_t id() const override { return "resources"_hs.id; }
        std::string_view name() const override { return "resources"; }
        void reset() override { records.clear(); saved.clear(); loaded.clear(); recordIndex.clear(); }
        bool save(core::ArchiveWriter&, core::SerializationContext&) override;
        bool load(core::ArchiveReader&, core::SerializationContext&) override;
    private:
        ResourceManager& _manager;
    };
    void describeResourceHandleMeta();
    void attachResources(core::SerializationContext&, ResourceManager&);

    // Reflection of the resource handle types (MeshHandle, MaterialHandle, ...).
    struct ResourceHandleMetaDescriber
    {
        static void describe() { describeResourceHandleMeta(); }
    };
    CS_REFLECT(ResourceHandleMetaDescriber);
}
