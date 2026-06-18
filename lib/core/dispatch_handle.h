#pragma once

#include "input_event.h"
#include "input_buffer.h"

namespace csyren::core::details
{
	class DispatchHandle
	{
	public:
		void dispatch(const input::InputEvent& event)
		{
			_eventBuffer.push(event);
		}
	protected:

		void setToMainWindow();
		InputBuffer<input::InputEvent> _eventBuffer;
	};
}