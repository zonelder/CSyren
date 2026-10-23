#ifndef __CSYREN_MOUSE_DEVICE__
#define __CSYREN_MOUSE_DEVICE__
#include "input_device.h"
#include "input_enums.h"

#include <bitset>


namespace csyren::core
{
	class MouseDevice final: public InputDevice
	{

	public:

		struct MouseDelta
		{
			int x{ 0 };
			int y{ 0 };
		};
		MouseDevice() noexcept = default;
		void update() override
		{
			_scrollDelta = 0;
			_delta.x = 0;
			_delta.y = 0;
		}

		DeviceType type() const override { return DeviceType::Mouse; }
		std::string name() const override { return "mouse"; }
		bool isConnected() const override { return true; }

		bool isButtonDown(MouseButton button) const noexcept { return getState(button); }
		bool isButtonUp(MouseButton button) const noexcept { return !getState(button); }

		// For internal use by InputDispatchSystem to generate events
		void setButtonState(MouseButton button, bool isDown)
		{
			_buttonStates[static_cast<size_t>(button)] = isDown;
		}

		void setButtonState(int code, bool isDown)
		{
			_buttonStates[code] = isDown;
		}

		// Called per move event: the delta accumulates over the frame (reset by update()).
		void setMousePosition(int x, int y)
		{
			if (_hasPosition)
			{
				_delta.x += x - _x;
				_delta.y += y - _y;
			}
			_hasPosition = true;
			_x = x;
			_y = y;
		}
		// Called per wheel event: the wheel delta (WHEEL_DELTA = 120 per notch) accumulates over the frame.
		void setScrollDelta(int delta) { _scrollDelta += delta; }

		// Movement since the previous frame, in client pixels.
		const MouseDelta& deltaPosition() const noexcept { return _delta; }
		int x() const noexcept { return _x; }
		int y() const noexcept { return _y; }
		// Wheel movement since the previous frame (positive: away from the user).
		int scrollDelta() const noexcept { return _scrollDelta; }

	private:
		bool getState(MouseButton button) const noexcept {
			return _buttonStates[static_cast<size_t>(button)];
		}
		std::bitset<static_cast<size_t>(MouseButton::Count)> _buttonStates;
		int _x{ 0 };
		int _y{ 0 };
		bool _hasPosition{ false };

		MouseDelta _delta;
		int _scrollDelta{ 0 };
	};
}

#endif
