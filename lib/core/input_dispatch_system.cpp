#include "pch.h"
#include "input_dispatch_system.h"
#include "services.h"



namespace csyren::core
{

	void InputDispatchSystem::init()
	{
		setToMainWindow();
	}

	void InputDispatchSystem::onAttach()
	{
		auto bus = &sceneBus();
		_actionToken = bus->register_publisher<InputAction>();

		using EventType = InputEvent::Type;
		_tokens[static_cast<uint32_t>(EventType::KeyDown)] =			bus->register_publisher<InputEvent>(static_cast<uint32_t>(EventType::KeyDown));
		_tokens[static_cast<uint32_t>(EventType::KeyUp)] =				bus->register_publisher<InputEvent>(static_cast<uint32_t>(EventType::KeyUp));
		_tokens[static_cast<uint32_t>(EventType::Keyhold)] =			bus->register_publisher<InputEvent>(static_cast<uint32_t>(EventType::Keyhold));
		_tokens[static_cast<uint32_t>(EventType::MouseButtonDown)] =	bus->register_publisher<InputEvent>(static_cast<uint32_t>(EventType::MouseButtonDown));
		_tokens[static_cast<uint32_t>(EventType::MouseButtonUp)] =		bus->register_publisher<InputEvent>(static_cast<uint32_t>(EventType::MouseButtonUp));
		_tokens[static_cast<uint32_t>(EventType::MouseMove)] =			bus->register_publisher<InputEvent>(static_cast<uint32_t>(EventType::MouseMove));
		_tokens[static_cast<uint32_t>(EventType::MouseWheel)] =			bus->register_publisher<InputEvent>(static_cast<uint32_t>(EventType::MouseWheel));
	}

CS_STRICT_SYSTEMS_BEGIN
	void InputDispatchSystem::execute(Res<Devices> devices, Res<EventBus2> bus)
	{
		InputAction action{ "","" };

		devices->preUpdate();
		while (!_eventBuffer.empty())
		{
			auto event = _eventBuffer.pop();
			// The device snapshot gets every event, published on the bus or not.
			devices->dispatchEvent(event);

			auto token_it = _tokens.find(static_cast<uint32_t>(event.type));
			if (token_it == _tokens.end()) continue; // no bus publisher for this event type

			if (auto currentContext = _contextManager.activeContext(event, action))
			{
				bus->publish(_actionToken, action);
			}
			bus->publish(token_it->second, event);
		}
	}
CS_STRICT_SYSTEMS_END



	void InputDispatchSystem::onDetach() noexcept
	{
		auto bus = &sceneBus();
		bus->unregister_publisher(_actionToken);

		for (auto [_, token] : _tokens)
		{
			bus->unregister_publisher(token);
		}
		_tokens.clear();
		while (!_eventBuffer.empty()) (void)_eventBuffer.pop();
	}
	void InputDispatchSystem::shutdown() {}
}
