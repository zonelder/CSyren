#ifndef __CSYREN_INPUT_DEVICE__
#define __CSYREN_INPUT_DEVICE__
#include <string>

namespace csyren::core
{
	enum class DeviceType
	{
		Keyboard,
		Mouse,
		Gamepad,
		Touch,
		Unknown,
	};

	class InputDevice
	{
	public:
		virtual ~InputDevice() = default;

		/**
		 * @brief Update the device state.
		 * This method should be called once per frame to update the device state.
		 */
		virtual void update() = 0;

		/**
		 * @brief Check if the device is connected..
		 * 
		 * @return true if the device is connected,false otherwise.
		 */
		virtual bool isConnected() const = 0;

		/**
		 * @brief Get the device name.
		 * 
		 * @return  The name of the device.
		 */
		virtual std::string name() const = 0;

		/**
		 * @brief Get device type.
		 * 
		 * @return The type of the device
		 */
		virtual DeviceType type() const = 0;


	};
}


#endif


