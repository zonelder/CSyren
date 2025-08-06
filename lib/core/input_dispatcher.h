#ifndef __CSYREN_EVENT_DISPATCHER__
#define __CSYREN_EVENT_DISPATCHER__
#include "input_event.h"
#include "input_context.h"
#include "input_buffer.h"
#include "devices.h"

#include "core/event_bus.h"

//todo make class a sender of
// InputEvent
// ActionEvent
namespace csyren::core::input
{
	class InputDispatcher
	{
	public:
		InputDispatcher(events::EventBus2& bus)
		{
			//for all type of input event input::InputType we shpul register a publisher
			//forget about eventDispatcher
			using InputType = input::InputEvent::Type;
			_publishers[static_cast<uint32_t>(InputType::KeyDown)] = bus.register_publisher<InputEvent>(static_cast<uint32_t>(InputType::KeyDown));
			_publishers[static_cast<uint32_t>(InputType::KeyUp)] = bus.register_publisher<InputEvent>(static_cast<uint32_t>(InputType::KeyUp));
			_publishers[static_cast<uint32_t>(InputType::MouseMove)] = bus.register_publisher<InputEvent>(static_cast<uint32_t>(InputType::MouseMove));
			_publishers[static_cast<uint32_t>(InputType::MouseButtonDown)] = bus.register_publisher<InputEvent>(static_cast<uint32_t>(InputType::MouseButtonDown));
			_publishers[static_cast<uint32_t>(InputType::MouseButtonUp)] = bus.register_publisher<InputEvent>(static_cast<uint32_t>(InputType::MouseButtonUp));
			_publishers[static_cast<uint32_t>(InputType::MouseWheel)] = bus.register_publisher<InputEvent>(static_cast<uint32_t>(InputType::MouseWheel));
			_publishers[static_cast<uint32_t>(InputType::GamepadButtonDown)] = bus.register_publisher<InputEvent>(static_cast<uint32_t>(InputType::GamepadButtonDown));
			_publishers[static_cast<uint32_t>(InputType::GamepadButtonUp)] = bus.register_publisher<InputEvent>(static_cast<uint32_t>(InputType::GamepadButtonUp));


			/////
			_actionToken = bus.register_publisher<InputAction>();
		}
		void update(events::EventBus2& bus)
		{
			InputAction action{ "","" };
			while (!_eventBuffer.empty())
			{
				auto event = _eventBuffer.pop();
				auto publisher_it = _publishers.find(static_cast<uint32_t>(event.type));
				if (publisher_it == _publishers.end())
				{
					log::error("InputDispatcher::update: unknown input event type {}", static_cast<uint32_t>(event.type));
					continue;
				}

				if (auto currentContext = _contextManager.activeContext(event, action))
				{
					bus.publish(_actionToken, action);

				}

				bus.publish(publisher_it->second, event);
				_devices.dispatchEvent(event);
			}

			_devices.update();

		}
		void dispatch(const InputEvent& event)
		{
			_eventBuffer.push(event);
		}

		InputContextManager& contexts() const noexcept { return const_cast<InputContextManager&>(_contextManager); }

		const Devices& devices() const noexcept { return _devices; }
	private:
		InputBuffer<InputEvent> _eventBuffer;
		Devices _devices;
		InputContextManager _contextManager;
		events::PublishToken _actionToken;
		std::unordered_map<uint32_t, events::PublishToken> _publishers;

	};
}


#endif
