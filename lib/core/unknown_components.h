#pragma once
// Components of types this program does not know (renamed, removed, or from code
// that is not built in), read from a scene or prefab. They are kept as data on
// the entity and written back when it is saved, duplicated or saved as a prefab,
// so opening and saving a document loses nothing.
//
// The scene serializer and the branch archive (source/runtime) fill and write
// it; it is not reflected, so it is neither saved under its own name nor offered
// in "Add component". The inspector lists the kept names.
#include <string>
#include <string_view>
#include <vector>

#include "archive.h"
#include "registration.h"

namespace csyren::core
{
    struct UnknownComponents
    {
        struct Entry
        {
            uint64_t id = 0;        // id of the component name in the archive
            std::string name;
            ArchiveValue value;
        };
        std::vector<Entry> entries; // in the order they were read

        const Entry* find(std::string_view name) const noexcept
        {
            for (const auto& entry : entries) if (entry.name == name) return &entry;
            return nullptr;
        }
        // Adds the entry, or replaces the one with the same name.
        void set(Entry entry)
        {
            for (auto& existing : entries)
                if (existing.name == entry.name) { existing = std::move(entry); return; }
            entries.push_back(std::move(entry));
        }
    };
    CS_COMPONENT(UnknownComponents);
}
