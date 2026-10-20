#pragma once
// Scene lighting components. Lights are ordinary entities: the Transform gives
// the position (point lights) and the forward (+Z) axis of the rotation gives
// the direction light travels (directional lights).
#include "math/math.h"
#include "core/reflected_enum.h"

namespace csyren::render
{
    // What the light pass outputs (LightingSettings::debugView).
    CS_ENUM(LightingDebugView, int, Lit, Albedo, Normals, RoughnessMetallic, Depth, ClusterLights, Emissive, LightingOnly)

    struct DirectionalLight
    {
        math::Color color{ 1.0f, 0.96f, 0.9f, 1.0f };
        float intensity = 3.0f;      // radiance scale (linear)
        bool enabled = true;
        static void describe();
    };

    struct PointLight
    {
        math::Color color{ 1.0f, 1.0f, 1.0f, 1.0f };
        float intensity = 20.0f;     // radiance at 1 m (linear); falls off with 1/d^2
        float radius = 10.0f;        // meters; light is windowed to zero at this distance
        bool enabled = true;
        static void describe();
    };

    // Scene-wide lighting/post-processing settings. Put one on any entity;
    // the first one found is used, defaults apply when none exists.
    struct LightingSettings
    {
        math::Color ambientColor{ 0.42f, 0.47f, 0.58f, 1.0f };
        float ambientIntensity = 0.35f;
        float exposure = 1.0f;
        bool clustered = true;       // false: the light pass loops over every light (debug/reference)
        LightingDebugView debugView = LightingDebugView::Lit;
        static void describe();
    };
}
