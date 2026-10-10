#include "core/meta_archive.h"
#include "core/meta_json.h"
#include <cassert>
#include <iostream>

struct Nested { int number = 7; };
struct Object { Nested nested; float fraction = 0.5f; int scratch = 12; };
struct Custom { uint64_t value{}; };

// A second backend verifies that registered type behavior is format-independent.
struct CountingWriter : csyren::core::ArchiveWriter
{
    size_t fields = 0, scalars = 0;
    bool object() override { return true; }
    bool beginField(uint64_t, std::string_view) override { ++fields; return true; }
    bool endField() override { return true; }
    bool endObject() override { return true; }
    bool sequence(size_t) override { return true; }
    bool beginElement(size_t) override { return true; }
    bool endElement() override { return true; }
    bool endSequence() override { return true; }
    bool boolean(bool) override { ++scalars; return true; }
    bool signedInteger(int64_t) override { ++scalars; return true; }
    bool unsignedInteger(uint64_t) override { ++scalars; return true; }
    bool floating(double) override { ++scalars; return true; }
    bool string(std::string_view) override { ++scalars; return true; }
};

int main()
{
    using namespace csyren::core;
    namespace meta = reflection;
    meta::MetaFactory<int>{}.primitiveType("int"_hs).archive(meta::scalarArchive<int>());
    meta::MetaFactory<float>{}.primitiveType("float"_hs).archive(meta::scalarArchive<float>());
    meta::MetaFactory<Nested>{}.type("Nested"_hs).data<&Nested::number>("number"_hs);
    meta::MetaFactory<Object>{}.type("Object"_hs).data<&Object::nested>("nested"_hs).data<&Object::fraction>("fraction"_hs)
        .data<&Object::scratch>("scratch"_hs, {}, meta::FieldFlags::Transient);
    SerializationContext context;
    Object original; nlohmann::json document;
    JsonArchiveWriter writer(document);
    assert(meta::save(writer, context, *meta::resolve<Object>(), &original));
    assert(!document.contains("scratch"));
    assert(!document.contains("$version") && !document["nested"].contains("$version"));
    Object restored; restored.nested.number = 0; restored.fraction = 0;
    JsonArchiveReader reader(document);
    assert(meta::load(reader, context, *meta::resolve<Object>(), &restored));
    assert(restored.nested.number == 7 && restored.fraction == 0.5f);
    CountingWriter secondBackend;
    assert(meta::save(secondBackend, context, *meta::resolve<Object>(), &original));
    assert(secondBackend.fields == 3 && secondBackend.scalars == 2);
    document["nested"]["number"] = "bad";
    JsonArchiveReader bad(document);
    assert(!meta::load(bad, context, *meta::resolve<Object>(), &restored));
    document["nested"].erase("number");
    JsonArchiveReader missing(document);
    restored.nested.number = 42;
    assert(meta::load(missing, context, *meta::resolve<Object>(), &restored));
    assert(restored.nested.number == 42);
    document["$version"] = 99;
    JsonArchiveReader future(document);
    assert(!meta::load(future, context, *meta::resolve<Object>(), &restored));
    // Open registration: custom semantic type, no enum or format specialization.
    meta::MetaFactory<Custom>{}.primitiveType("Custom"_hs).archive({
        [](ArchiveWriter& w, SerializationContext&, const meta::MetaType&, const void* p) { return w.unsignedInteger(static_cast<const Custom*>(p)->value + 1); },
        [](ArchiveReader& r, SerializationContext&, const meta::MetaType&, void* p) { uint64_t v; if (!r.unsignedInteger(v) || v == 0) return false; static_cast<Custom*>(p)->value = v - 1; return true; }
    });
    Custom custom{ 123 }, decoded;
    JsonArchiveWriter customWriter(document);
    assert(meta::save(customWriter, context, *meta::resolve<Custom>(), &custom));
    assert(document == 124);
    assert(meta::save(secondBackend, context, *meta::resolve<Custom>(), &custom));
    assert(secondBackend.scalars == 3);
    JsonArchiveReader customReader(document);
    assert(meta::load(customReader, context, *meta::resolve<Custom>(), &decoded) && decoded.value == 123);
    int integer;
    document = uint64_t(-1); JsonArchiveReader overflow(document);
    assert(!meta::load(overflow, context, *meta::resolve<int>(), &integer));
    // Sequential field cursors nest without restarting the outer traversal.
    nlohmann::json fields = {
        {"$version", 1}, {"a", {{"$version", 1}, {"x", 11}, {"y", 12}}},
        {"b", 22}, {"skip", {{"deep", {{"unused", 99}}}}}
    };
    JsonArchiveReader cursor(fields);
    uint64_t fieldID{}; std::string fieldName; bool present{}; int64_t value{};
    assert(cursor.beginFields());
    assert(!cursor.beginFields());
    assert(cursor.nextField(fieldID, fieldName, present) && present && fieldName == "a");
    assert(fieldID == "a"_hs.id);
    assert(!cursor.nextField(fieldID, fieldName, present)); // Finish current field first.
    assert(cursor.beginFields());
    assert(cursor.nextField(fieldID, fieldName, present) && present && fieldName == "x");
    assert(cursor.signedInteger(value) && value == 11 && cursor.endField());
    assert(cursor.nextField(fieldID, fieldName, present) && present && fieldName == "y");
    assert(cursor.signedInteger(value) && value == 12 && cursor.endField());
    assert(cursor.nextField(fieldID, fieldName, present) && !present);
    assert(!cursor.endField()); // Cannot leave an object with an active cursor.
    assert(!cursor.endObject());
    assert(cursor.endFields() && cursor.endField());
    assert(cursor.nextField(fieldID, fieldName, present) && present && fieldName == "b");
    assert(cursor.signedInteger(value) && value == 22 && cursor.endField());
    assert(cursor.nextField(fieldID, fieldName, present) && present && fieldName == "skip");
    assert(cursor.endField()); // Unknown object can be skipped as a whole.
    assert(cursor.nextField(fieldID, fieldName, present) && !present);
    assert(cursor.nextField(fieldID, fieldName, present) && !present);
    assert(cursor.endFields() && cursor.endObject());
    assert(!cursor.endFields());
    assert(!cursor.nextField(fieldID, fieldName, present));
    // Empty and metadata-only objects expose no logical fields.
    for (auto empty : { nlohmann::json::object(), nlohmann::json{{"$version", 1}} }) {
        JsonArchiveReader emptyReader(empty);
        assert(emptyReader.beginFields());
        assert(emptyReader.nextField(fieldID, fieldName, present) && !present);
        assert(emptyReader.endFields());
    }
    JsonArchiveReader scalar(document);
    assert(!scalar.beginFields());
    // A wide object is visited once, without copying/indexing its field list.
    nlohmann::json wide = {{"$version", 1}};
    for (int i = 0; i < 2048; ++i) wide["field" + std::to_string(i)] = i;
    JsonArchiveReader wideReader(wide);
    assert(wideReader.beginFields());
    size_t visited = 0;
    int64_t sum = 0;
    for (;;) {
        assert(wideReader.nextField(fieldID, fieldName, present));
        if (!present) break;
        assert(wideReader.signedInteger(value)); sum += value; ++visited;
        assert(wideReader.endField());
    }
    assert(visited == 2048 && sum == int64_t(2047) * 2048 / 2);
    assert(wideReader.endFields());
    // Closing early is allowed, including when nested.
    JsonArchiveReader early(fields);
    assert(early.beginFields());
    assert(early.nextField(fieldID, fieldName, present) && present);
    assert(early.beginFields() && early.endFields() && early.endField());
    assert(early.endFields() && early.endObject());
    std::cout << "meta archive checks passed\n";
}
