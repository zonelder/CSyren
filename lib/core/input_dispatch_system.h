#ifndef __CSYREN_EVENT_DISPATCHER__
#define __CSYREN_EVENT_DISPATCHER__
#include "input_context.h"
#include "devices.h"
#include "event_bus.h"
#include "system_base.h"
#include "dispatch_handle.h"

#include <unordered_map>

//todo make class a sender of
// InputEvent
// ActionEvent++
namespace csyren::core
{
	class InputDispatchSystem : public System,private details::DispatchHandle
	{
	public:
		void init() override;

		void shutdown() override;

		void update() override;

	private:
		input::InputContextManager _contextManager;

		events::PublishToken _actionToken;
		std::unordered_map<uint32_t, events::PublishToken> _tokens;

	};
}


#endif
