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
        core::TaskDescription describe() const override {
            core::TaskDescription d; d.phase = "simulation";
            d.read<DebugRotator>().write<core::Transform>().readResource<core::Time>(); return d;
        }

        explicit DebugRotatorSystem() = default;

#pragma optimize("",off)
        void execute() override
        {
            using ctx = core::Services;
            auto time = ctx::get<const core::Time>();
            auto scene = ctx::get<core::Scene>();
            float dt = time->deltaTime();
            float totalTime = time->totalTime();
            constexpr float waveSpeed = 2.0f;
            constexpr float waveLength = 3.0f;   
            constexpr float amplitude = 0.1f;    
            scene->view<Transform, const DebugRotator>().each([&](Entity::ID entt, Transform& tr, const DebugRotator& rot)
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

