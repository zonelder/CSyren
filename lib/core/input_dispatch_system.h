#ifndef __CSYREN_EVENT_DISPATCHER__
#define __CSYREN_EVENT_DISPATCHER__
#include "input_context.h"
#include "devices.h"
#include "event_bus.h"
#include "system_base.h"
#include "dispatch_handle.h"

#include <unordered_map>
#include "registration.h"

//todo make class a sender of
// InputEvent
// ActionEvent++
namespace csyren::core
{
	class InputDispatchSystem : public System,private details::DispatchHandle
	{
	public:
        core::TaskDescription describe() const override;

		void init() override;

		void shutdown() override;

		void execute() override;
	protected:
		void onAttach() override;
		void onDetach() noexcept override;

	private:
		InputContextManager _contextManager;

		PublishToken _actionToken;
		std::unordered_map<uint32_t, PublishToken> _tokens;

	};

	CS_SYSTEM(InputDispatchSystem);
}


#endif
