#ifndef __CSYREN_EVENT_DISPATCHER__
#define __CSYREN_EVENT_DISPATCHER__
#include "input_event.h"
#include "input_context.h"
#include "input_buffer.h"
#include "devices.h"
#include "event_bus.h"

#include <unordered_map>

//todo make class a sender of
// InputEvent
// ActionEvent++
namespace csyren::core::input
{
	class InputDispatcher
	{
	public:
		InputDispatcher() noexcept = default;

		void init(events::EventBus2& bus)
		{

			_actionToken = bus.register_publisher<InputAction>();

			using EventType = input::InputEvent::Type;
			_tokens[static_cast<uint32_t>(EventType::KeyDown)]			 = bus.register_publisher<input::InputEvent>(static_cast<uint32_t>(EventType::KeyDown));
			_tokens[static_cast<uint32_t>(EventType::KeyUp)]			 = bus.register_publisher<input::InputEvent>(static_cast<uint32_t>(EventType::KeyUp));
			_tokens[static_cast<uint32_t>(EventType::Keyhold)]			 = bus.register_publisher<input::InputEvent>(static_cast<uint32_t>(EventType::Keyhold));
			_tokens[static_cast<uint32_t>(EventType::MouseButtonDown)]   = bus.register_publisher<input::InputEvent>(static_cast<uint32_t>(EventType::MouseButtonDown));
			_tokens[static_cast<uint32_t>(EventType::MouseButtonUp)]	 = bus.register_publisher<input::InputEvent>(static_cast<uint32_t>(EventType::MouseButtonUp));
			_tokens[static_cast<uint32_t>(EventType::MouseMove)]		 = bus.register_publisher<input::InputEvent>(static_cast<uint32_t>(EventType::MouseMove));
		}

		void shutdown(events::EventBus2& bus)
		{
			bus.unregister_publisher(_actionToken);

			for (auto [_, token] : _tokens)
			{
				bus.unregister_publisher(token);
			}
		}

		void update(events::EventBus2& bus)
		{
			InputAction action{ "","" };
			while (!_eventBuffer.empty())
			{
				auto event = _eventBuffer.pop();

				auto token_it = _tokens.find(static_cast<uint32_t>(event.type));
				if (token_it == _tokens.end())
				{
					log::error("Unregister input event find. event type = {}", static_cast<uint32_t>(event.type));
					continue;
				}

				if (auto currentContext = _contextManager.activeContext(event, action))
				{
					bus.publish(_actionToken, action);
				}
				bus.publish(token_it->second, event);
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
		std::unordered_map<uint32_t, events::PublishToken> _tokens;

	};
}


#endif
