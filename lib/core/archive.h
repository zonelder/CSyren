#pragma once
#include <cstdint>
#include <string>
#include <string_view>

namespace csyren::core
{
    // The format vocabulary is independent of engine types and their policies.
    class ArchiveWriter
    {
    public:
        virtual ~ArchiveWriter() = default;
        // Compact (binary) formats store stable ids instead of names (enum
        // values are written as name hashes). Text formats keep names.
        virtual bool compact() const { return false; }
        virtual bool object() = 0;
        virtual bool beginField(uint64_t id, std::string_view name) = 0;
        virtual bool endField() = 0;
        virtual bool endObject() = 0;
        virtual bool sequence(size_t size) = 0;
        virtual bool beginElement(size_t index) = 0;
        virtual bool endElement() = 0;
        virtual bool endSequence() = 0;
        virtual bool boolean(bool) = 0;
        virtual bool signedInteger(int64_t) = 0;
        virtual bool unsignedInteger(uint64_t) = 0;
        virtual bool floating(double) = 0;
        virtual bool string(std::string_view) = 0;
    };
    class ArchiveReader
    {
    public:
        virtual ~ArchiveReader() = default;
        virtual bool compact() const { return false; }
        virtual bool object(uint32_t& version) = 0;
        // Missing fields are distinct from errors and retain the object's defaults.
        virtual bool beginField(uint64_t id, std::string_view name, bool& present) = 0;
        // Sequential logical fields, excluding format housekeeping. Cursors nest.
        // nextField enters a field when present=true; pair it with endField before
        // advancing. Success with present=false means end, false means an error.
        // endFields closes the cursor (and may skip unread fields).
        virtual bool beginFields() = 0;
        virtual bool nextField(uint64_t& id, std::string& name, bool& present) = 0;
        virtual bool endFields() = 0;
        virtual bool endField() = 0;
        virtual bool endObject() = 0;
        virtual bool sequence(size_t& size) = 0;
        virtual bool beginElement(size_t index) = 0;
        virtual bool endElement() = 0;
        virtual bool endSequence() = 0;
        virtual bool boolean(bool&) = 0;
        virtual bool signedInteger(int64_t&) = 0;
        virtual bool unsignedInteger(uint64_t&) = 0;
        virtual bool floating(double&) = 0;
        virtual bool string(std::string&) = 0;
    };
}
