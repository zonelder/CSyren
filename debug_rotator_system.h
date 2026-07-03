#pragma once

#include "core/event_bus.h"
#include "core/services.h"
#include "core/system_base.h"
#include "core/scene.h"

#include "debug_rotator.h"
#include "core/transform.h"
#include "math/math.h"
#include "core/time.h"

using namespace csyren::core;
using namespace csyren;

namespace csyren
{
    class DebugRotatorSystem : public core::System
    {
    public:
        explicit DebugRotatorSystem() = default;

#pragma optimize("",off)
        void update() override
        {
            using ctx = core::Services;
            auto time = ctx::get<core::Time>();
            auto scene = ctx::get<core::Scene>();
            float dt = time->deltaTime();
            float totalTime = time->totalTime();
            constexpr float waveSpeed = 2.0f;
            constexpr float waveLength = 3.0f;   
            constexpr float amplitude = 0.1f;    
            scene->view<Transform, DebugRotator>().each([&](Entity::ID entt, Transform& tr, DebugRotator& rot)
                {
                    tr.rotation *= math::Quaternion(DirectX::XMQuaternionRotationRollPitchYaw(rot.speed.x * dt, rot.speed.y * dt, rot.speed.z * dt));

                    float phase = (tr.position[0] + tr.position[2]) / waveLength;
                    float height = sinf(totalTime * waveSpeed - phase) * amplitude;
                    tr.position += height*Vector3::up;
                });
        }
    };

    REGISTER_SYSTEM(DebugRotatorSystem);
}

