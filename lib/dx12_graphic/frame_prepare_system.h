#pragma once
#include "core/system_base.h"

namespace csyren::render
{
    namespace details { struct PresentationState; }

    class FrameAcquireSystem : public core::System {
    public:
        core::TaskDescription describe() const override;
        void init() override;
        void execute() override;
        void shutdown() override;
    private:
        void onAttach() override;
        void onDetach() noexcept override;
        void updateCamera();
        std::shared_ptr<details::PresentationState> _state;
    };
    REGISTER_SYSTEM(FrameAcquireSystem)

    class FramePrepareSystem : public core::System
    {
    public:
        core::TaskDescription describe() const override;

        void init() override;
        void execute() override;
        void shutdown() override;
    private:
        std::shared_ptr<details::PresentationState> _state;
    };

    REGISTER_SYSTEM(FramePrepareSystem)
}
