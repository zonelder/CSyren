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

    private:
        core::Entity::ID _selectedEntity = core::Entity::invalidID;

    };
}