#pragma once
#include "core/scene.h"
#include "imGui/imgui.h"


namespace csyren::editor
{
    class ComponentEditor
    {
    public:
        virtual ~ComponentEditor() = default;

        virtual const char* name() const = 0;

        virtual void render(core::Entity* entity, core::Scene* scene) = 0;
    };
}