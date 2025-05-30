#ifndef __CSYREN_WINKEBOAR_DDEVICE__
#define __CSYREN_WINKEBOAR_DDEVICE__

#include "input_device.h"
#include "input_buffer.h"
#include "input_event.h"

#include <bitset>
#include <Windows.h>

namespace csyren::core
{
	class Win32KeyboardDevice : public InputDevice
	{
	public:
		Win32KeyboardDevice() noexcept = default;

        /** update system key states before message pump */
        void earlyUpdate() override
        {
            _shiftDown = GetKeyState(VK_SHIFT) & 0x8000;
            _ctrlDown = GetKeyState(VK_CONTROL) & 0x8000;
            _altDown = GetKeyState(VK_MENU) & 0x8000;
        }

		void update() override
		{
            while (!_eventBuffer.empty())
            {
                //TODO pass event to input ahndler
                _eventBuffer.pop();
            }
		}

		bool isConnected() const override
		{
			return GetKeyboardState(nullptr) != FALSE;
		}

		DeviceType type() const override { return DeviceType::Keyboard; }
		std::string name() const override { return "Win32 Keyboard"; }

		bool isKeyDown(int key) const
		{
			return _keyStates[key];
		}

        // Called from WindowProc
        void handleWindowsMessage(UINT msg, WPARAM wParam, LPARAM lParam) {
            using EventType = InputEvent::Type;
            InputEvent::Type eventType = EventType::KeyDown;
            bool isRepeat = false;

            switch (msg) {
            case WM_KEYDOWN:
            case WM_SYSKEYDOWN:
                eventType = (lParam & 0x40000000) ?
                    EventType::Keyhold :
                    EventType::KeyDown;
                _keyStates[wParam] = true;
                break;

            case WM_KEYUP:
            case WM_SYSKEYUP:
                eventType = EventType::KeyUp;
                _keyStates[wParam] = false;
                break;

            default:
                return;
            }

            // Create input event
            InputEvent event;
            event.source = this;
            event.type = eventType;
            event.code = static_cast<int>(wParam);
            event.data.keyboard.shift = _shiftDown;
            event.data.keyboard.ctrl = _ctrlDown;
            event.data.keyboard.alt = _altDown;
            _eventBuffer.push(event);
        }

	private:
		static constexpr unsigned int _nKeys = 256u;
		std::bitset<_nKeys> _keyStates;
		InputBuffer<InputEvent> _eventBuffer;
		bool _shiftDown = false;
		bool _ctrlDown = false;
		bool _altDown = false;
	};
}



#endif
