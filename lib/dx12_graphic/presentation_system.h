#pragma once
#include <array>
#include "core/typed_system.h"
#include "core/registration.h"
#include "dx12_graphic/renderer.h"

namespace csyren::render
{
    namespace details { struct PresentationState; struct FrameRecording; }

    // Submits the recorded GPU plan and hands the frame to the output worker;
    // moves the frame token (FrameRecording) from "ready" to "submitted".
    class PresentationSystem : public core::TypedSystem<PresentationSystem>
    {
    public:
        static constexpr auto phase = "submit";
        static constexpr core::TaskAffinity affinity = core::TaskAffinity::MainThread;
        static constexpr std::array states{ core::StateUse::write<details::FrameRecording>("ready", "submitted") };

        void init() override;
        // The GPU plan submission records into the Renderer.
        void execute(core::Res<Renderer> renderer);
        void shutdown() override;
    private:
        std::shared_ptr<details::PresentationState> _state;
    };

    CS_SYSTEM(PresentationSystem);
}
