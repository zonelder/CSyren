#pragma once
#include "core/entity.h"

namespace csyren::editor
{
    class EditorSelection
    {
    public:
        void select(core::Entity::ID id) { _selectedEntity = id; }
        [[nodiscard]] core::Entity::ID selected() const noexcept { return _selectedEntity; }

        [[nodiscard]] bool hasSelection() const noexcept { return _selectedEntity != core::Entity::invalidID; }
        void clearSelection() noexcept { _selectedEntity = core::Entity::invalidID; }
        [[nodiscard]] bool isSelected(core::Entity::ID id) const noexcept { return _selectedEntity == id; }

    private:
        core::Entity::ID _selectedEntity = core::Entity::invalidID;

    };
}