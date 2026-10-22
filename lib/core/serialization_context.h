#pragma once

#include "type_key.h"
#include <unordered_map>
#include <memory>
#include <vector>
#include "archive.h"

namespace csyren::core
{
    class SerializationContext;
    class SerializationSection
    {
    public:
        virtual ~SerializationSection() = default;
        virtual uint64_t id() const = 0;
        virtual std::string_view name() const = 0;
        virtual void reset() = 0;
        virtual bool save(ArchiveWriter&, SerializationContext&) = 0;
        virtual bool load(ArchiveReader&, SerializationContext&) = 0;
        virtual bool finishLoad(SerializationContext&) { return true; }
    };
    // Per save/load operation service bag. It deliberately owns no engine state.
    class SerializationContext
    {
    public:
        // Expose staging data to custom load/fixup operations without retaining
        // a dangling pointer when the operation ends (including exceptions).
        template<class Service> class ScopedService
        {
            SerializationContext& context;
            Service* previous;
        public:
            ScopedService(SerializationContext& owner, Service& service) : context(owner), previous(owner.tryGet<Service>()) { owner.provide(service); }
            ScopedService(const ScopedService&) = delete;
            ~ScopedService() {
                if (previous) context.provide(*previous);
                else context._services.erase(TypeKey::of<Service>());
            }
        };
        template<class Service> ScopedService<Service> scopedService(Service& service) { return { *this, service }; }
        template<class Service, class... Args> Service& emplaceService(Args&&... args)
        {
            auto service = std::make_shared<Service>(std::forward<Args>(args)...);
            auto* raw = service.get();
            _ownedServices.insert_or_assign(TypeKey::of<Service>(), std::move(service));
            provide(*raw);
            return *raw;
        }
        // Owns module extensions for the whole operation, exposing one context.
        template<class Section, class... Args> void attachSection(Args&&... args)
        {
            auto section = std::make_shared<Section>(std::forward<Args>(args)...);
            provide(*section);
            for (auto& existing : _sections)
                if (existing->id() == section->id()) { existing = std::move(section); return; }
            _sections.push_back(std::move(section));
        }
        void reset() { for (auto& section : _sections) section->reset(); }
        bool saveSections(ArchiveWriter& writer) {
            for (auto& section : _sections) {
                if (!writer.beginField(section->id(), section->name())) return false;
                const bool ok = section->save(writer, *this);
                if (!writer.endField() || !ok) return false;
            }
            return true;
        }
        bool loadSections(ArchiveReader& reader) {
            for (auto& section : _sections) {
                bool present{};
                if (!reader.beginField(section->id(), section->name(), present)) return false;
                if (!present) continue;
                const bool ok = section->load(reader, *this);
                if (!reader.endField() || !ok) return false;
            }
            return true;
        }
        bool finishLoad() { for (auto& section : _sections) if (!section->finishLoad(*this)) return false; return true; }
        template<class Service> void provide(Service& service)
        {
            _services.insert_or_assign(TypeKey::of<Service>(), &service);
        }
        template<class Service> Service* tryGet() const noexcept
        {
            const auto it = _services.find(TypeKey::of<Service>());
            return it == _services.end() ? nullptr : static_cast<Service*>(it->second);
        }
    private:
        std::unordered_map<TypeKey, void*> _services;
        std::vector<std::shared_ptr<SerializationSection>> _sections;
        std::unordered_map<TypeKey, std::shared_ptr<void>> _ownedServices;
    };
}
