#pragma once
#include "core/entity.h"
#include <cstdint>

namespace csyren::editor
{
    class EditorSelection
    {
    public:
        void synchronize(uint64_t instance)
        { if (_instance != instance) { clearSelection(); _instance = instance; } }
        void select(core::Entity::ID id) { _selectedEntity = id; }
        [[nodiscard]] core::Entity::ID selected() const noexcept { return _selectedEntity; }

        [[nodiscard]] bool hasSelection() const noexcept { return _selectedEntity != core::Entity::invalidID; }
        void clearSelection() noexcept { _selectedEntity = core::Entity::invalidID; }
        [[nodiscard]] bool isSelected(core::Entity::ID id) const noexcept { return _selectedEntity == id; }

    private:
        core::Entity::ID _selectedEntity = core::Entity::invalidID;
        uint64_t _instance = 0;

    };
}
