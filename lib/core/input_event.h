#ifndef __CSYREN_INPUT_EVENT__
#define __CSYREN_INPUT_EVENT__
#include "input_device.h"
#include "input_enums.h"

#include <functional>
#include <chrono>
#include <memory>
#include <algorithm>

namespace csyren::core::input
{
	/**
	 * 
	 * @brief Represents an input event from any input device
	 * 
	 * This class encapsulate all inpormation about an input event.
	 * .
	 */
	struct InputEvent
	{
	public:

		InputEvent() noexcept = default;
		/**
		 * @enum Type
		 * @brief Enumaterion of all possible input event types.
		 */
		enum class Type
		{
			KeyDown,			///< Keyboard key pressed
			KeyUp,				///< Keyboard key released
			Keyhold,			///< Keyboard key held down
			MouseMove,			///< Mouse cursor move
			MouseButtonDown,	///< Mouse button pressed
			MouseButtonUp,		///< Mouse button released
			MouseWheel,			///< Mouse wheel scrolled
			GamepadButtonDown,	///< Gamepad button pressed
			GamepadButtonUp,	///< Gamepad button released
			GamedapAxisMove,	///< gamepad analog stick or trigger moved
			Touch,				///< Touch screen event
			DeviceConneected,	///< Input device connected
			DeviceDisconnected,	///< Input device disconnected
			Custom,				///< Custom event type
		};

		Type type;				///< The type of event
		DeviceType deviceType{ DeviceType::Unknown };
		double timestamp;		///< When the event occured(in seconds)
		int code;				///< Device specific code(e.g. key code)

		// Event-specific data
		union {
			struct {
				int x;
				int y;
				int deltaX;
				int deltaY;
				int wheelDelta;
			} mouse;

			struct {
				bool shift;
				bool ctrl;
				bool alt;
				bool system;
			} keyboard;

			struct {
				float value;
				int axis;
			} gamepad;

			struct {
				int x;
				int y;
				int id;
				float pressure;
			} touch;

			void* customData;
		} data;

		InputEvent(Type type, DeviceType device, int code = 0) noexcept :
			type(type),
			deviceType(device),
			code(code)
		{
			using namespace std::chrono;
			auto now = high_resolution_clock::now();
			timestamp = duration_cast<duration<double>>(now.time_since_epoch()).count();
			memset(&data, 0, sizeof(data));
		}
	};

	/**
	 * @class InputEventDispatcher
	 * @brief Dispatcher input events to registered callbacks
	 * 
	 * This class manages event subscription and handles the dispatching
	 * of events to appropriate callbacks based of event type and priority
	 */
	class InputEventDispatcher
	{
	public:
		/**
		 * @typedef EventCallback_t
		 * @brief Function signatura for event callbacks
		 */
		using EventCallback_t = std::function<void(const InputEvent&)>;

		/**
		 * @brief Represent a subscription to an event type.
		 */
		struct Subscription
		{
			EventCallback_t callback;
			int priority;
			bool operator<(const Subscription& other) const noexcept { return priority < other.priority; }
			bool operator==(const Subscription& other) const noexcept { return priority == other.priority; }
		};

		/**
		 * @brief Subscribe to an event type.
		 * @param type The event type to subscribe to
		 * @param callback The callback function in invoke
		 * @priority The priority of the subscription(higher = called earlier)
		 */
		void subscribe(InputEvent::Type type, EventCallback_t callback, int priority = 0)
		{
			auto& subs = _subscribers[type];
			subs.push_back({ callback,priority });
			std::sort(subs.begin(), subs.end());
		}

		/**
		 * @brief Unsubscribe from an event type.
		 * 
		 * @param type The event typ
		 * @param callback The callback to remove
		 */
		void unsubscribe(InputEvent::Type type, EventCallback_t callback)
		{
			//TODO implement
			/*
			//bat implementation. better choose other container 
			auto& subs = _subscribers[type];
			auto it = std::find(subs.begin(), subs.end(), callback);
			if (it == subs.end())
				return;
			subs.erase(it);
			*/
		}

		/**
		 * .@brief Dispatch an event to all subscribers
		 * @param event The event to dispatch
		 * @return true if the event was handled,false otherwise.
		 */
		bool dispatch(const InputEvent& event)
		{
			auto it = _subscribers.find(event.type);
			if (it == _subscribers.end())
				return false;

			for (const auto& sub : it->second)
			{
				sub.callback(event);
			}
		}
	private:
		std::unordered_map<InputEvent::Type, std::vector<Subscription>> _subscribers;
	};
}



#endif
