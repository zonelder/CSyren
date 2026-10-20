#include "pch.h"
#include "resource_handle_meta.h"
#include "resource_manager.h"
#include "core/meta_archive.h"

namespace csyren::render
{
    void attachResources(core::SerializationContext& context, ResourceManager& manager)
    {
        context.attachSection<ResourceSerializationContext>(manager);
    }
    bool ResourceSerializationContext::save(core::ArchiveWriter& writer, core::SerializationContext& context)
    {
        if (!writer.sequence(records.size())) return false;
        for (size_t i = 0; i < records.size(); ++i) {
            if (!writer.beginElement(i)) return false;
            const bool ok = core::reflection::save(writer, context, *core::reflection::resolve<ResourceRecord>(), &records[i]);
            if (!writer.endElement() || !ok) return false;
        }
        return writer.endSequence();
    }
    bool ResourceSerializationContext::load(core::ArchiveReader& reader, core::SerializationContext& context)
    {
        size_t count{};
        if (!reader.sequence(count)) return false;
        records.clear(); records.reserve(count);
        for (size_t i = 0; i < count; ++i) {
            ResourceRecord record;
            if (!reader.beginElement(i)) return false;
            const bool ok = core::reflection::load(reader, context, *core::reflection::resolve<ResourceRecord>(), &record);
            if (!reader.endElement() || !ok || record.type.empty() || record.path.empty()) return false;
            records.push_back(std::move(record));
        }
        return reader.endSequence() && indexRecords();
    }
    bool ResourceSerializationContext::indexRecords()
    {
        recordIndex.clear(); loaded.clear();
        for (size_t i = 0; i < records.size(); ++i)
            if (!recordIndex.emplace(records[i].id, i).second) return false;
        return true;
    }
    namespace
    {
        template<class Resource, auto GetName>
        core::reflection::MetaArchiveOps referenceArchive()
        {
            using Handle = THandle<Resource>;
            return {
                [](core::ArchiveWriter& writer, core::SerializationContext& context, const core::reflection::MetaType& type, const void* value) -> bool {
                    auto* resources = context.tryGet<ResourceSerializationContext>();
                    if (!resources) return false;
                    const auto handle = *static_cast<const Handle*>(value);
                    if (!handle) return writer.unsignedInteger(Handle::INVALID);
                    const ResourceKey key{ type.id.id, handle.id, handle.generation };
                    const auto& path = (resources->manager().*GetName)(handle);
                    if (path.empty()) return false;
                    if (auto it = resources->saved.find(key); it != resources->saved.end()) return writer.unsignedInteger(it->second);
                    const uint64_t id = resources->records.size();
                    resources->records.push_back({ id, std::string(type.name), path });
                    resources->saved.emplace(key, id);
                    return writer.unsignedInteger(id);
                },
                [](core::ArchiveReader& reader, core::SerializationContext& context, const core::reflection::MetaType& type, void* value) -> bool {
                    auto* resources = context.tryGet<ResourceSerializationContext>(); uint64_t id{};
                    if (!resources || !reader.unsignedInteger(id)) return false;
                    if (id == Handle::INVALID) { *static_cast<Handle*>(value) = Handle{ Handle::INVALID }; return true; }
                    const auto entry = resources->recordIndex.find(id);
                    if (entry == resources->recordIndex.end()) return false;
                    const auto& record = resources->records[entry->second];
                    if (record.type != type.name) return false;
                    if (auto it = resources->loaded.find(id); it != resources->loaded.end()) {
                        const Handle handle{ it->second.id, it->second.generation };
                        if ((resources->manager().*GetName)(handle).empty()) return false;
                        *static_cast<Handle*>(value) = handle; return true;
                    }
                    const Handle handle = resources->manager().get<Resource>(record.path);
                    if (!handle) return false;
                    resources->loaded.emplace(id, ResourceIdentity{ handle.id, handle.generation }); *static_cast<Handle*>(value) = handle; return true;
                }
            };
        }
    }
    void describeResourceHandleMeta()
    {
        namespace meta = core::reflection;
        meta::MetaFactory<uint64_t>{}.primitiveType("uint64"_hs).archive(meta::scalarArchive<uint64_t>());
        meta::MetaFactory<std::string>{}.primitiveType("string"_hs).archive(meta::scalarArchive<std::string>());
        meta::MetaFactory<ResourceRecord>{}.primitiveType("ResourceRecord"_hs)
            .archive({ &meta::saveFields, &meta::loadFields })
            .data<&ResourceRecord::id>("id"_hs, {}, meta::FieldFlags::Serializable | meta::FieldFlags::Required)
            .data<&ResourceRecord::type>("type"_hs, {}, meta::FieldFlags::Serializable | meta::FieldFlags::Required)
            .data<&ResourceRecord::path>("path"_hs, {}, meta::FieldFlags::Serializable | meta::FieldFlags::Required);
        meta::MetaFactory<TextureHandle>{}.primitiveType("TextureHandle"_hs, "texture").archive(referenceArchive<Texture, &ResourceManager::getTextureName>());
        meta::MetaFactory<MeshHandle>{}.primitiveType("MeshHandle"_hs, "mesh").archive(referenceArchive<Mesh, &ResourceManager::getMeshName>());
        meta::MetaFactory<MaterialHandle>{}.primitiveType("MaterialHandle"_hs, "material").archive(referenceArchive<Material, &ResourceManager::getMaterialName>());
        meta::MetaFactory<ShaderHandle>{}.primitiveType("ShaderHandle"_hs, "shader").archive(referenceArchive<GraphicShader, &ResourceManager::getShaderName>());
        meta::MetaFactory<AnimationClipHandle>{}.primitiveType("AnimationClipHandle"_hs, "animation").archive(referenceArchive<AnimationClip, &ResourceManager::getAnimationClipName>());
    }
}
