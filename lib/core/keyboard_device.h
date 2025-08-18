#ifndef __CSYREN_WINKEBOAR_DDEVICE__
#define __CSYREN_WINKEBOAR_DDEVICE__

#include "input_device.h"
#include "input_buffer.h"
#include "input_event.h"

#include <bitset>
#include <Windows.h>

namespace csyren::core::input
{
	class KeyboardDevice : public InputDevice
	{
	public:
		KeyboardDevice() noexcept = default;
		bool isConnected() const override
		{
			return true;
		}

		void update() override {};

		DeviceType type() const override { return DeviceType::Keyboard; }
		std::string name() const override { return "Keyboard"; }

		bool isKeyDown(int key) const
		{
			return _keyStates[key];
		}

		bool isKeyDown(KeyCode key) const
		{
			return _keyStates[static_cast<int>(key)];
		}

        bool isKeyUp(int key) const
        {
            return !_keyStates[key];
        }

		bool isKeyUp(KeyCode key) const
		{
			return !_keyStates[static_cast<int>(key)];
		}

        //this method should never be give to user of engine. input system only

        void setKeyDown(int key)
        {
            _keyStates[key] = true;
        }
        void setKeyUp(int key) { _keyStates[key] = false; }
	private:
		static constexpr unsigned int _nKeys = 256u;
		std::bitset<_nKeys> _keyStates;
	};
}



#endif
