#include "pch.h"
#include "input_dispatch_system.h"
#include "services.h"



namespace csyren::core
{

	void InputDispatchSystem::init()
	{
		setToMainWindow();
		auto bus = Services::get<events::EventBus2>();
		_actionToken = bus->register_publisher<input::InputAction>();

		using EventType = input::InputEvent::Type;
		_tokens[static_cast<uint32_t>(EventType::KeyDown)] =			bus->register_publisher<input::InputEvent>(static_cast<uint32_t>(EventType::KeyDown));
		_tokens[static_cast<uint32_t>(EventType::KeyUp)] =				bus->register_publisher<input::InputEvent>(static_cast<uint32_t>(EventType::KeyUp));
		_tokens[static_cast<uint32_t>(EventType::Keyhold)] =			bus->register_publisher<input::InputEvent>(static_cast<uint32_t>(EventType::Keyhold));
		_tokens[static_cast<uint32_t>(EventType::MouseButtonDown)] =	bus->register_publisher<input::InputEvent>(static_cast<uint32_t>(EventType::MouseButtonDown));
		_tokens[static_cast<uint32_t>(EventType::MouseButtonUp)] =		bus->register_publisher<input::InputEvent>(static_cast<uint32_t>(EventType::MouseButtonUp));
		_tokens[static_cast<uint32_t>(EventType::MouseMove)] =			bus->register_publisher<input::InputEvent>(static_cast<uint32_t>(EventType::MouseMove));
	}

	void InputDispatchSystem::update()
	{
		input::InputAction action{ "","" };
		auto devices = Services::get<input::Devices>();
		auto bus = Services::get<events::EventBus2>();

		devices->preUpdate();
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
				bus->publish(_actionToken, action);
			}
			bus->publish(token_it->second, event);
			devices->dispatchEvent(event);
		}
	}



	void InputDispatchSystem::shutdown()
	{
		auto bus = Services::get<events::EventBus2>();
		bus->unregister_publisher(_actionToken);

		for (auto [_, token] : _tokens)
		{
			bus->unregister_publisher(token);
		}
	}
}