#pragma once
#include "core/scene.h"
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
        template<class T> T* field(EntityKey key, size_t family, const FieldPath& path) const
        {
            auto* info = core::reflection::ComponentRegistry::get(family);
            auto* type = info ? info->metaType : nullptr;
            void* value = scene.getComponentRaw(resolve(key), family);
            for (auto id : path)
            {
                if (!type || !value) return nullptr;
                const core::reflection::MetaData* found{};
                for (const auto& f : type->data) if (f.id.id == id) { found = &f; break; }
                if (!found || !found->get) return nullptr;
                value = found->get(value); type = found->type;
            }
            return type == core::reflection::resolve<T>() ? static_cast<T*>(value) : nullptr;
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
    };
    template<class T> class FieldEdit final : public PendingFieldEdit
    {
    public:
        T before, after;
        FieldEdit(EntityKey target, size_t component, FieldPath field, T previous, T next)
            : before(std::move(previous)), after(std::move(next))
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
            *destination = value; return true;
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
