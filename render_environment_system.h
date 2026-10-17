#pragma once
#include "core/system_base.h"
#include "core/services.h"
#include "core/time.h"
#include "dx12_graphic/primitives.h"
#include "dx12_graphic/renderer.h"

namespace csyren::render
{
    class RenderEnvironmentSystem : public core::System
    {
    public:
        core::TaskDescription describe() const override {
            core::TaskDescription d; d.phase = "acquire"; d.before = {"FrameAcquireSystem"};
            d.affinity = core::TaskAffinity::MainThread;
            d.writeResource<Renderer>().readResource<core::Time>(); return d;
        }
        void execute() override { updateLight(); }
        void init() override
        {
            // Register factories before the startup scene resolves its resources.
            if (!Primitives::registerFabricsAll())
                throw std::runtime_error("Failed to register primitive resource factories");
        }
        void onFrame() override { updateLight(); }
        static DirectX::XMFLOAT4 lightDirection(float time) noexcept
        {
            const auto base = DirectX::XMVectorSet(0.3f, -1.0f, 0.3f, 0.0f);
            const auto rotation = DirectX::XMMatrixRotationZ(time * DirectX::XM_2PI / 10.0f);
            const auto direction = DirectX::XMVector3Normalize(DirectX::XMVector3TransformNormal(base, rotation));
            DirectX::XMFLOAT4 result;
            DirectX::XMStoreFloat4(&result, direction);
            return result;
        }
    protected:
        void onAttach() override { updateLight(); }
    private:
        static void updateLight()
        {
            core::Services::get<Renderer>()->getEngineVariableBuffer()->lightDirection =
                lightDirection(core::Services::get<const core::Time>()->totalTime());
        }
    };
    REGISTER_SYSTEM(RenderEnvironmentSystem)
}
