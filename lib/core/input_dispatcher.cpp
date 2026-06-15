#include "pch.h"
#include "input_dispatcher.h"
#include "services.h"

namespace csyren::core::input
{
	void InputDispatcher::update(events::EventBus2& bus)
	{
		InputAction action{ "","" };
		auto devices = Services::get<Devices>();
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
				bus.publish(_actionToken, action);
			}
			bus.publish(token_it->second, event);
			devices->dispatchEvent(event);
		}
	}
}