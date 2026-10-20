#pragma once
#include "core/system_base.h"
#include "core/registration.h"

namespace csyren::render
{
    namespace details { struct PresentationState; }

    class PresentationSystem : public core::System
    {
    public:
        core::TaskDescription describe() const override;

        void init() override;
        void execute() override;
        void shutdown() override;
    private:
        std::shared_ptr<details::PresentationState> _state;
    };

    CS_SYSTEM(PresentationSystem);
}
