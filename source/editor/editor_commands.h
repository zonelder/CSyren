#pragma once
#include "core/scene.h"
#include "core/entity_ref.h"
#include "core/type_key.h"
#include "core/meta_json.h"
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>
#include <DirectXMath.h>

namespace csyren::editor
{
    using EntityKey = uint64_t;
    using EntityMap = std::unordered_map<EntityKey, core::Entity::ID>;
    using FieldPath = std::vector<uint64_t>;
    struct EditContext
    {
        core::Scene& scene;
        EntityMap& entities;
        core::Entity::ID resolve(EntityKey key) const
        {
            auto it = entities.find(key);
            return it == entities.end() ? core::Entity::invalidID : it->second;
        }
        // Field addressed by `path` inside a component, with its reflected type.
        void* fieldRaw(EntityKey key, size_t family, const FieldPath& path, const core::reflection::MetaType** fieldType) const
        {
            auto* info = core::reflection::ComponentRegistry::get(family);
            const core::reflection::MetaType* type = info ? info->metaType : nullptr;
            void* value = scene.getComponentRaw(resolve(key), family);
            for (auto id : path)
            {
                if (!type || !value) return nullptr;
                const core::reflection::MetaData* found{};
                for (const auto& f : type->data) if (f.id.id == id) { found = &f; break; }
                if (!found || !found->get) return nullptr;
                value = found->get(value); type = found->type;
            }
            if (fieldType) *fieldType = type;
            return value;
        }
        template<class T> T* field(EntityKey key, size_t family, const FieldPath& path) const
        {
            const core::reflection::MetaType* type{};
            void* value = fieldRaw(key, family, path, &type);
            return value && type == core::reflection::resolve<T>() ? static_cast<T*>(value) : nullptr;
        }
    };
    class EditorCommand
    {
    public:
        virtual ~EditorCommand() = default;
        virtual bool execute(EditContext&) = 0;
        virtual bool undo(EditContext&) = 0;
        virtual const char* label() const = 0;
    };
    class FunctionCommand final : public EditorCommand
    {
    public:
        using Action = std::function<bool(EditContext&)>;
        FunctionCommand(std::string name, Action apply, Action revert)
            : _name(std::move(name)), _apply(std::move(apply)), _revert(std::move(revert)) {}
        bool execute(EditContext& context) override { return _apply(context); }
        bool undo(EditContext& context) override { return _revert(context); }
        const char* label() const override { return _name.c_str(); }
    private:
        std::string _name;
        Action _apply, _revert;
    };
    class PendingFieldEdit : public EditorCommand
    {
    public:
        EntityKey key{};
        size_t family{};
        FieldPath path;
        virtual bool changed() const = 0;
        const char* label() const override { return "Edit component"; }
        // Downcast without RTTI: every concrete edit records its own type key.
        template<class Edit> Edit* as() noexcept
        { return _kind == core::TypeKey::of<Edit>() ? static_cast<Edit*>(this) : nullptr; }
    protected:
        explicit PendingFieldEdit(core::TypeKey kind) : _kind(kind) {}
    private:
        core::TypeKey _kind;
    };
    template<class T> class FieldEdit final : public PendingFieldEdit
    {
    public:
        T before, after;
        FieldEdit(EntityKey target, size_t component, FieldPath field, T previous, T next)
            : PendingFieldEdit(core::TypeKey::of<FieldEdit>()), before(std::move(previous)), after(std::move(next))
        { key = target; family = component; path = std::move(field); }
        bool changed() const override
        {
            // Math operator== uses an epsilon; history must retain even tiny edits.
            if constexpr (requires(const T& value) { static_cast<DirectX::XMVECTOR>(value); })
                return !DirectX::XMVector4Equal(static_cast<DirectX::XMVECTOR>(before), static_cast<DirectX::XMVECTOR>(after));
            else return !(before == after);
        }
        bool execute(EditContext& context) override { return apply(context, after); }
        bool undo(EditContext& context) override { return apply(context, before); }
    private:
        bool apply(EditContext& context, const T& value)
        {
            auto* destination = context.field<T>(key, family, path);
            if (!destination) return false;
            *destination = value;
            return context.scene.patch(context.resolve(key), family);
        }
    };
    // Type-erased field edit: before/after are stored through the field type's
    // reflection archive. Used by editors that do not know the C++ type
    // (reflected enums, masks, named enums).
    class ArchivedFieldEdit final : public PendingFieldEdit
    {
    public:
        const core::reflection::MetaType* type{};
        nlohmann::json before, after;
        ArchivedFieldEdit(EntityKey target, size_t component, FieldPath field, const core::reflection::MetaType* fieldType, nlohmann::json previous, nlohmann::json next)
            : PendingFieldEdit(core::TypeKey::of<ArchivedFieldEdit>()), type(fieldType), before(std::move(previous)), after(std::move(next))
        { key = target; family = component; path = std::move(field); }
        bool changed() const override { return before != after; }
        bool execute(EditContext& context) override { return apply(context, after); }
        bool undo(EditContext& context) override { return apply(context, before); }
        static nlohmann::json capture(const core::reflection::MetaType& fieldType, const void* value)
        {
            nlohmann::json result;
            core::JsonArchiveWriter writer(result);
            core::SerializationContext serialization;
            core::reflection::save(writer, serialization, fieldType, value);
            return result;
        }
    private:
        bool apply(EditContext& context, const nlohmann::json& value)
        {
            const core::reflection::MetaType* fieldType{};
            void* destination = context.fieldRaw(key, family, path, &fieldType);
            if (!destination || fieldType != type) return false;
            core::JsonArchiveReader reader(value);
            core::SerializationContext serialization;
            if (!core::reflection::load(reader, serialization, *type, destination)) return false;
            return context.scene.patch(context.resolve(key), family);
        }
    };
    // Edit of an EntityRef field. before/after are the targets' editor keys (0:
    // none): runtime IDs change when the editor scene is rebuilt, keys do not.
    class EntityRefFieldEdit final : public PendingFieldEdit
    {
    public:
        EntityKey before{}, after{};
        EntityRefFieldEdit(EntityKey target, size_t component, FieldPath field, EntityKey previous, EntityKey next)
            : PendingFieldEdit(core::TypeKey::of<EntityRefFieldEdit>()), before(previous), after(next)
        { key = target; family = component; path = std::move(field); }
        bool changed() const override { return before != after; }
        bool execute(EditContext& context) override { return apply(context, after); }
        bool undo(EditContext& context) override { return apply(context, before); }
    private:
        bool apply(EditContext& context, EntityKey value)
        {
            auto* destination = context.field<core::EntityRef>(key, family, path);
            if (!destination) return false;
            destination->id = value ? context.resolve(value) : core::Entity::invalidID;
            return context.scene.patch(context.resolve(key), family);
        }
    };
    class CommandHistory
    {
        struct Entry { std::unique_ptr<EditorCommand> command; uint64_t before, after; };
    public:
        bool execute(std::unique_ptr<EditorCommand> command, EditContext& context)
        {
            _entries.reserve(_cursor + 1);
            if (!command->execute(context)) return false;
            record(std::move(command)); return true;
        }
        void record(std::unique_ptr<EditorCommand> command)
        {
            _entries.reserve(_cursor + 1);
            _entries.erase(_entries.begin() + _cursor, _entries.end());
            const auto next = ++_next;
            _entries.push_back({ std::move(command), _current, next });
            _current = next; ++_cursor;
        }
        bool undo(EditContext& context)
        {
            if (!canUndo() || !_entries[_cursor - 1].command->undo(context)) return false;
            _current = _entries[--_cursor].before; return true;
        }
        bool redo(EditContext& context)
        {
            if (!canRedo() || !_entries[_cursor].command->execute(context)) return false;
            _current = _entries[_cursor++].after; return true;
        }
        bool canUndo() const { return _cursor != 0; }
        bool canRedo() const { return _cursor != _entries.size(); }
        bool dirty() const { return _current != _saved; }
        void markSaved() { _saved = _current; }
        void reset() { _entries.clear(); _cursor = 0; _current = _saved = ++_next; }
    private:
        std::vector<Entry> _entries;
        size_t _cursor{};
        uint64_t _next{}, _current{}, _saved{};
    };
}
