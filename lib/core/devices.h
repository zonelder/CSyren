#ifndef __CSYREN_INPUT__
#define __CSYREN_INPUT__
#include "keyboard_device.h"
#include "mouse_device.h"

namespace csyren::core::input
{
	class Devices
	{
		friend class InputDispatcher;
	public:

		const KeyboardDevice& keyboard() const noexcept { return _keyboard; }
		const MouseDevice& mouse() const noexcept { return _mouse; }
	private:
		void update()
		{
			_keyboard.update();
		}
		void dispatchEvent(const InputEvent& event)
		{
			switch (event.type)
			{
			case InputEvent::Type::KeyDown:
				_keyboard.setKeyDown(event.code);
				break;
			case InputEvent::Type::KeyUp:
				_keyboard.setKeyUp(event.code);

			case InputEvent::Type::Keyhold:
				//TODO implement
				break;
			case InputEvent::Type::MouseButtonDown:
				_mouse.setButtonState(event.code, true);
			case InputEvent::Type::MouseButtonUp:
				_mouse.setButtonState(event.code, false);
			case InputEvent::Type::MouseWheel:
				_mouse.setScrollDelta(event.data.mouse.wheelDelta);
			case InputEvent::Type::MouseMove:
				_mouse.setMousePosition(event.data.mouse.x, event.data.mouse.y);
			default:
				break;
			}
		}
		KeyboardDevice _keyboard;
		MouseDevice _mouse;
	};
}



#endif
