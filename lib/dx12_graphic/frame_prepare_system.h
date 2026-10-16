#pragma once
#include "core/system_base.h"

namespace csyren::render
{
    namespace details { struct PresentationState; }

    class FramePrepareSystem : public core::System
    {
    public:
        void init() override;
        void onFrame() override;
        void shutdown() override;
    private:
        void updateCamera(float (&background)[4]);
        std::shared_ptr<details::PresentationState> _state;
    };

    REGISTER_SYSTEM(FramePrepareSystem)
}
