#pragma once

#include "input_event.h"
#include "input_buffer.h"

namespace csyren::core::details
{
	class DispatchHandle
	{
	public:
		// Consecutive mouse moves are merged into the latest one (MouseDevice keeps
		// the position, the delta is accumulated from positions), so a fast mouse at a
		// low frame rate cannot push key and button events out of the buffer.
		void dispatch(const InputEvent& event)
		{
			if (event.type == InputEvent::Type::MouseMove && !_eventBuffer.empty()
				&& _eventBuffer.back().type == InputEvent::Type::MouseMove)
			{
				_eventBuffer.back() = event;
				return;
			}
			_eventBuffer.push(event);
		}
	protected:

		void setToMainWindow();
		InputBuffer<InputEvent> _eventBuffer{ 256 };
	};
}