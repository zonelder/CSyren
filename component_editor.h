#pragma once
#include <cstddef>
#include "core/scene.h"
#include "core/component_base.h"
#include "imGui/imgui.h"

namespace csyren::editor
{
    class ComponentEditorBase
    {
    public:
        virtual ~ComponentEditorBase() = default;
        virtual const char* name() const = 0;
        virtual void render(core::Entity::ID entity, core::Scene* scene) = 0;
    };

    // Ўаблонный редактор Ч знает тип компонента
    template<typename T>
    class ComponentEditor : public ComponentEditorBase
    {
    public:
        using comp_type = T;
        void render(core::Entity::ID entity, core::Scene* scene) override
        {
            auto id = entity->id;
            if (!scene->hasComponent<comp_type>(id)) return;

            auto c = scene->getComponent<comp_type>(id);
            if (!c) return;

            onInspect(*c, entity, scene);
        }
    protected:
        virtual void onInspect(T& comp, core::Entity::ID entity, core::Scene* scene) = 0;
    };

    // ѕустой редактор Ч fallback
    template<typename T>
    class EmptyEditor : public ComponentEditor<T>
    {
    protected:
        void onInspect(T&, core::Entity::ID, core::Scene*) override
        {
            ImGui::TextDisabled("No editor for this component");
        }
    };

  
}