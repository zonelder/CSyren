#pragma once

#include "core/event_bus.h"
#include "core/context.h"
#include "core/system_base.h"
#include "core/scene.h"

#include "debug_rotator.h"
#include "transform.h"
#include "math/math.h"
#include "core/time.h"

using namespace csyren::core;
using namespace csyren::components;

namespace csyren
{
    class DebugRotatorSystem : public core::System
    {
    public:
        explicit DebugRotatorSystem() = default;

        void update(events::UpdateEvent& event) override
        {
            float dt = event.time.deltaTime();
            float totalTime = event.time.totalTime();
            constexpr float waveSpeed = 2.0f;
            constexpr float waveLength = 3.0f;   
            constexpr float amplitude = 0.1f;    
            event.scene.view<Transform, DebugRotator>().each([&](Entity::ID entt, Transform& tr, DebugRotator& rot)
                {
                    tr.rotation *= math::Quaternion(DirectX::XMQuaternionRotationRollPitchYaw(rot.speed.x * dt, rot.speed.y * dt, rot.speed.z * dt));

                    float phase = (tr.position[0] + tr.position[2]) / waveLength;
                    float height = sinf(totalTime * waveSpeed - phase) * amplitude;
                    tr.position += height*Vector3::up;
                });
        }
    };
}

