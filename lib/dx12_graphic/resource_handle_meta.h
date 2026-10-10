#pragma once
#include "resource_handle.h"
#include "core/meta.h"
#include <optional>
#include <unordered_map>
#include <vector>
#include <string>

namespace csyren::render
{
    class ResourceManager;
    struct ResourceRecord { uint64_t id{}; std::string type; std::string path; };
    struct ResourceKey { uint64_t type; uint64_t runtimeID; bool operator==(const ResourceKey&) const = default; };
    struct ResourceKeyHash { size_t operator()(ResourceKey key) const noexcept { return std::hash<uint64_t>{}(key.type) ^ (std::hash<uint64_t>{}(key.runtimeID) << 1); } };
    class ResourceSerializationContext : public core::SerializationSection
    {
    public:
        explicit ResourceSerializationContext(ResourceManager& manager) : _manager(manager) {}
        std::vector<ResourceRecord> records;
        std::unordered_map<ResourceKey, uint64_t, ResourceKeyHash> saved;
        std::unordered_map<uint64_t, uint64_t> loaded;
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
}
