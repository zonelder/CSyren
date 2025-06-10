#ifndef __CSYREN_MOUSE_DEVICE__
#define __CSYREN_MOUSE_DEVICE__
#include "input_device.h"
#include "input_enums.h"

#include <bitset>

namespace csyren::core::input
{
	class MouseDevice final: public InputDevice
	{
	public:
		MouseDevice() noexcept = default;
		void update() override
		{
			_scrollDelta = 0;
		}

		DeviceType type() const override { return DeviceType::Mouse; }
		std::string name() const override { return "mouse"; }
		bool isConnected() const override { return true; }

		bool isButtonDown(MouseButton button) const noexcept { return getState(button); }
		bool isButtonUp(MouseButton button) const noexcept { return !getState(button); }

		// For internal use by InputDispatcher to generate events
		void setButtonState(MouseButton button, bool isDown)
		{
			_buttonStates[static_cast<size_t>(button)] = isDown;
		}

		void setButtonState(int code, bool isDown)
		{
			_buttonStates[code] = isDown;
		}

		void setMousePosition(int x, int y)
		{
			_x = x;
			_y = y;
		}
		void setScrollDelta(int delta) { _scrollDelta = delta; }

	private:
		bool getState(MouseButton button) const noexcept {
			return _buttonStates[static_cast<size_t>(button)];
		}
		std::bitset<static_cast<size_t>(MouseButton::Count)> _buttonStates;
		int _x{ 0 };
		int _y{ 0 };
		int _scrollDelta{ 0 };
	};
}

#endif
