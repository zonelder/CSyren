#ifndef __CSYREN_EVENT_DISPATCHER__
#define __CSYREN_EVENT_DISPATCHER__
#include "input_event.h"
#include "input_context.h"
#include "input_buffer.h"
#include "devices.h"
#include "event_bus.h"

#include <unordered_map>

//todo make class a sender of
// InputEvent
// ActionEvent++
namespace csyren::core::input
{
	class InputDispatcher
	{
	public:
		InputDispatcher() noexcept = default;

		void init();

		void shutdown();

		void update();

		void dispatch(const InputEvent& event)
		{
			_eventBuffer.push(event);
		}
		InputContextManager& contexts() const noexcept { return const_cast<InputContextManager&>(_contextManager); }
	private:
		InputBuffer<InputEvent> _eventBuffer;
		InputContextManager _contextManager;

		events::PublishToken _actionToken;
		std::unordered_map<uint32_t, events::PublishToken> _tokens;

	};
}


#endif
