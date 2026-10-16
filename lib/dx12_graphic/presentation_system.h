#pragma once
#include "core/system_base.h"

namespace csyren::render
{
    namespace details { struct PresentationState; }

    class PresentationSystem : public core::System
    {
    public:
        void init() override;
        void onFrame() override;
        void shutdown() override;
    private:
        std::shared_ptr<details::PresentationState> _state;
    };

    REGISTER_SYSTEM(PresentationSystem)
}
