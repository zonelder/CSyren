#pragma once
#include "meta.h"
#include "third_party/json/json.hpp"
#include <vector>
#include <limits>

namespace csyren::core
{
    class JsonArchiveWriter final : public ArchiveWriter
    {
        using Json = nlohmann::json;
        Json* _value;
        std::vector<Json*> _parents;
    public:
        explicit JsonArchiveWriter(Json& value) : _value(&value) {}
        bool object() override { *_value = Json::object(); return true; }
        bool beginField(uint64_t, std::string_view name) override { _parents.push_back(_value); _value = &(*_value)[std::string(name)]; return true; }
        bool endField() override { if (_parents.empty()) return false; _value = _parents.back(); _parents.pop_back(); return true; }
        bool endObject() override { return true; }
        bool sequence(size_t size) override { *_value = Json::array(); _value->get_ref<Json::array_t&>().resize(size); return true; }
        bool beginElement(size_t index) override { if (!_value->is_array() || index >= _value->size()) return false; _parents.push_back(_value); _value = &(*_value)[index]; return true; }
        bool endElement() override { return endField(); }
        bool endSequence() override { return true; }
        bool boolean(bool value) override { *_value = value; return true; }
        bool signedInteger(int64_t value) override { *_value = value; return true; }
        bool unsignedInteger(uint64_t value) override { *_value = value; return true; }
        bool floating(double value) override { *_value = value; return true; }
        bool string(std::string_view value) override { *_value = std::string(value); return true; }
    };
    class JsonArchiveReader final : public ArchiveReader
    {
        using Json = nlohmann::json;
        const Json* _value;
        std::vector<const Json*> _parents;
        struct FieldCursor {
            const Json* owner;
            Json::const_iterator next;
            size_t depth;
        };
        std::vector<FieldCursor> _fields;
        bool atCursorOwner() const noexcept {
            return !_fields.empty() && _fields.back().owner == _value && _fields.back().depth == _parents.size();
        }
    public:
        explicit JsonArchiveReader(const Json& value) : _value(&value) {}
        bool object(uint32_t& version) override {
            if (!_value->is_object()) return false;
            auto it = _value->find("$version"); version = 1;
            if (it == _value->end()) return true;
            JsonArchiveReader reader(*it); uint64_t v{};
            if (!reader.unsignedInteger(v) || v > (std::numeric_limits<uint32_t>::max)()) return false;
            version = static_cast<uint32_t>(v); return true;
        }
        bool beginField(uint64_t, std::string_view name, bool& present) override { if (!_value->is_object()) return false; auto it = _value->find(std::string(name)); present = it != _value->end(); if (present) { _parents.push_back(_value); _value = &*it; } return true; }
        bool beginFields() override {
            if (!_value->is_object() || atCursorOwner()) return false;
            _fields.push_back({ _value, _value->cbegin(), _parents.size() });
            return true;
        }
        bool nextField(uint64_t& id, std::string& name, bool& present) override {
            present = false;
            if (!atCursorOwner()) return false;
            auto& cursor = _fields.back();
            while (cursor.next != cursor.owner->cend() && cursor.next.key() == "$version") ++cursor.next;
            if (cursor.next == cursor.owner->cend()) { id = 0; name.clear(); return true; }
            const auto it = cursor.next++;
            name = it.key(); id = cstdmf::fnv1a_32(name.data(), name.size()).id;
            _parents.push_back(_value); _value = &it.value(); present = true;
            return true;
        }
        bool endFields() override {
            if (!atCursorOwner()) return false;
            _fields.pop_back(); return true;
        }
        bool endField() override { if (_parents.empty() || atCursorOwner()) return false; _value = _parents.back(); _parents.pop_back(); return true; }
        bool endObject() override { return !atCursorOwner(); }
        bool sequence(size_t& size) override { if (!_value->is_array()) return false; size = _value->size(); return true; }
        bool beginElement(size_t index) override { if (!_value->is_array() || index >= _value->size()) return false; _parents.push_back(_value); _value = &(*_value)[index]; return true; }
        bool endElement() override { return endField(); }
        bool endSequence() override { return true; }
        bool boolean(bool& value) override { if (!_value->is_boolean()) return false; value = _value->get<bool>(); return true; }
        bool signedInteger(int64_t& value) override { if (!_value->is_number_integer()) return false; if (_value->is_number_unsigned() && _value->get<uint64_t>() > static_cast<uint64_t>((std::numeric_limits<int64_t>::max)())) return false; value = _value->get<int64_t>(); return true; }
        bool unsignedInteger(uint64_t& value) override { if (!_value->is_number_integer()) return false; if (!_value->is_number_unsigned() && _value->get<int64_t>() < 0) return false; value = _value->get<uint64_t>(); return true; }
        bool floating(double& value) override { if (!_value->is_number()) return false; value = _value->get<double>(); return true; }
        bool string(std::string& value) override { if (!_value->is_string()) return false; value = _value->get<std::string>(); return true; }
    };
}
