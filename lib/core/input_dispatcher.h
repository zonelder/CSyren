#ifndef __CSYREN_EVENT_DISPATCHER__
#define __CSYREN_EVENT_DISPATCHER__
#include "input_event.h"
#include "input_context.h"
#include "input_buffer.h"
#include "devices.h"

//todo make class a sender of
// InputEvent
// ActionEvent
namespace csyren::core::input
{
	class InputDispatcher
	{
	public:
		InputDispatcher() noexcept = default;
		void update()
		{
			InputAction action{ "","" };
			while (!_eventBuffer.empty())
			{
				auto event = _eventBuffer.pop();

				if (auto currentContext = _contextManager.activeContext(event, action))
				{
					dispatchActionEvent(event, action);
				}
				_eventDispatcher.dispatch(event);
				_devices.dispatchEvent(event);
			}

			_devices.update();

		}
		void dispatch(const InputEvent& event)
		{
			_eventBuffer.push(event);
		}

		InputEventDispatcher& events() const noexcept { return const_cast<InputEventDispatcher&>(_eventDispatcher); }
		InputContextManager& contexts() const noexcept { return const_cast<InputContextManager&>(_contextManager); }

		const Devices& devices() const noexcept { return _devices; }
	private:
		void dispatchActionEvent(const InputEvent& event, const InputAction& action)
		{
			
			
		}
		InputBuffer<InputEvent> _eventBuffer;
		Devices _devices;
		InputEventDispatcher _eventDispatcher;
		InputContextManager _contextManager;

	};
}


#endif
