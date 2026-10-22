#pragma once
// Gathers the scene's light components into the SceneLights snapshot and the
// renderer's RenderEnvironment before the camera views are prepared.
#include <algorithm>
#include <array>
#include "core/typed_system.h"
#include "core/transform.h"
#include "core/transform_hierarchy.h"
#include "dx12_graphic/renderer.h"
#include "dx12_graphic/lighting/light_components.h"
#include "dx12_graphic/lighting/scene_lights.h"
#include "core/registration.h"

CS_STRICT_SYSTEMS_BEGIN
namespace csyren::render
{
    class LightCollectSystem : public core::TypedSystem<LightCollectSystem>
    {
    public:
        static constexpr auto phase = "acquire";
        static constexpr std::array before{ "FrameAcquireSystem" };
        static constexpr core::TaskAffinity affinity = core::TaskAffinity::MainThread;

        void execute(core::Select<LightingSettings> lightingSettings,
            core::Select<core::Transform, DirectionalLight> directionalLights,
            core::Select<core::Transform, PointLight> pointLights,
            core::Res<SceneLights> sceneLights, core::Res<Renderer> output)
        {
            using namespace DirectX;
            auto& lights = *sceneLights;
            auto& renderer = *output; // before any change: a missing service leaves lights untouched
            _worlds.update(directionalLights); // any query that reads Transform

            lights.lights.clear();
            lights.directionalCount = 0; lights.pointCount = 0;
            RenderEnvironment environment;

            // Settings: first component wins.
            for (auto [id, settings] : lightingSettings)
            {
                XMFLOAT4 ambient; XMStoreFloat4(&ambient, settings.ambientColor);
                environment.ambientColor = { ambient.x * settings.ambientIntensity, ambient.y * settings.ambientIntensity, ambient.z * settings.ambientIntensity, 0.0f };
                environment.exposure = (std::max)(settings.exposure, 0.0f);
                environment.clustered = settings.clustered;
                environment.debugView = static_cast<int>(settings.debugView);
                break;
            }

            for (auto [id, transform, light] : directionalLights)
            {
                if (!light.enabled || lights.directionalCount >= SceneLights::MaxDirectional) continue;
                const auto& world = _worlds.matrix(id);
                auto direction = world.multiplyVector(math::Vector3::forward);
                if (direction.sqrMagnitude() < 1e-8f) direction = math::Vector3::down;
                direction.normalize();
                GpuLight gpu{};
                gpu.type = 0;
                gpu.direction = { direction.x, direction.y, direction.z };
                XMFLOAT4 color; XMStoreFloat4(&color, light.color);
                gpu.color = { color.x * light.intensity, color.y * light.intensity, color.z * light.intensity };
                if (lights.directionalCount == 0)
                {
                    environment.lightDirection = { direction.x, direction.y, direction.z, 0.0f };
                    environment.sunColor = { gpu.color.x, gpu.color.y, gpu.color.z, 1.0f };
                }
                lights.lights.push_back(gpu);
                ++lights.directionalCount;
            }

            for (auto [id, transform, light] : pointLights)
            {
                if (!light.enabled || light.radius <= 0.0f || lights.lights.size() >= SceneLights::MaxLights) continue;
                const auto position = _worlds.matrix(id).translation();
                GpuLight gpu{};
                gpu.type = 1;
                gpu.position = { position.x, position.y, position.z };
                gpu.radius = light.radius;
                gpu.invRadiusSq = 1.0f / (light.radius * light.radius);
                XMFLOAT4 color; XMStoreFloat4(&color, light.color);
                gpu.color = { color.x * light.intensity, color.y * light.intensity, color.z * light.intensity };
                lights.lights.push_back(gpu);
                ++lights.pointCount;
            }

            environment.directionalCount = lights.directionalCount;
            environment.pointCount = lights.pointCount;
            renderer.environment() = environment;
            ++lights.revision;
        }
    private:
        core::WorldTransforms _worlds;
    };
    CS_SYSTEM(LightCollectSystem);
}
CS_STRICT_SYSTEMS_END
