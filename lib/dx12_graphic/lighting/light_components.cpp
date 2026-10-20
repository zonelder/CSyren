// Compiled without the precompiled header (see dx12_graphic.vcxproj).
#include "light_components.h"
#include "core/meta.h"
#include "core/meta_archive.h"

namespace csyren::render
{
    void DirectionalLight::describe()
    {
        core::reflection::MetaFactory<DirectionalLight>{}.type("DirectionalLight"_hs)
            .data<&DirectionalLight::color>("color"_hs)
            .data<&DirectionalLight::intensity>("intensity"_hs)
            .data<&DirectionalLight::enabled>("enabled"_hs);
    }

    void PointLight::describe()
    {
        core::reflection::MetaFactory<PointLight>{}.type("PointLight"_hs)
            .data<&PointLight::color>("color"_hs)
            .data<&PointLight::intensity>("intensity"_hs)
            .data<&PointLight::radius>("radius"_hs)
            .data<&PointLight::enabled>("enabled"_hs);
    }

    void LightingSettings::describe()
    {
        core::reflection::MetaFactory<LightingSettings>{}.type("LightingSettings"_hs)
            .data<&LightingSettings::ambientColor>("ambientColor"_hs)
            .data<&LightingSettings::ambientIntensity>("ambientIntensity"_hs)
            .data<&LightingSettings::exposure>("exposure"_hs)
            .data<&LightingSettings::clustered>("clustered"_hs)
            .data<&LightingSettings::debugView>("debugView"_hs);
    }
}
