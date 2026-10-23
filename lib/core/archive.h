#pragma once
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

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
    // A value of a type the program does not know (e.g. a component of a type that
    // was renamed or removed), kept as data so it can be written back unchanged.
    struct ArchiveValue
    {
        enum class Kind : uint8_t { None, Boolean, Signed, Unsigned, Floating, String, Object, Sequence };
        struct Field;
        Kind kind = Kind::None;
        bool boolean = false;
        int64_t signedInteger = 0;
        uint64_t unsignedInteger = 0;
        double floating = 0.0;
        std::string string;
        std::vector<Field> fields;          // Object, in document order
        std::vector<ArchiveValue> elements; // Sequence
    };
    struct ArchiveValue::Field
    {
        uint64_t id = 0;
        std::string name;
        ArchiveValue value;
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
        // Reads the current value whatever its type (formats that cannot tell the
        // type of a value return false, and unknown values are dropped).
        virtual bool capture(ArchiveValue&) { return false; }
    };

    // Writes a captured value back (Kind::None writes nothing).
    inline bool write(ArchiveWriter& writer, const ArchiveValue& value)
    {
        using Kind = ArchiveValue::Kind;
        switch (value.kind)
        {
        case Kind::None: return true;
        case Kind::Boolean: return writer.boolean(value.boolean);
        case Kind::Signed: return writer.signedInteger(value.signedInteger);
        case Kind::Unsigned: return writer.unsignedInteger(value.unsignedInteger);
        case Kind::Floating: return writer.floating(value.floating);
        case Kind::String: return writer.string(value.string);
        case Kind::Object:
            if (!writer.object()) return false;
            for (const auto& field : value.fields)
                if (!writer.beginField(field.id, field.name) || !write(writer, field.value) || !writer.endField()) return false;
            return writer.endObject();
        case Kind::Sequence:
            if (!writer.sequence(value.elements.size())) return false;
            for (size_t i = 0; i < value.elements.size(); ++i)
                if (!writer.beginElement(i) || !write(writer, value.elements[i]) || !writer.endElement()) return false;
            return writer.endSequence();
        }
        return false;
    }
}
