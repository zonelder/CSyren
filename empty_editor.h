#pragma once

#include "component_editor.h"

namespace csyren::editor
{
    template<class T>
    class EmptyEditor : public ComponentEditor
    {
        using comp_type = T;
    public:
        const char* name() const override { return typeid(T).name(); }

        void render(core::Entity* entity, core::Scene* scene) override
        {
        }
    };
}