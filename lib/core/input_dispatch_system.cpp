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
		_actions = sceneBus().createEvent<InputAction>();
		_events = sceneBus().createEvent<InputEvent>();
	}

CS_STRICT_SYSTEMS_BEGIN
	void InputDispatchSystem::execute(Res<Devices> devices)
	{
		InputAction action{ "","" };

		devices->preUpdate();
		while (!_eventBuffer.empty())
		{
			auto event = _eventBuffer.pop();
			// The device snapshot gets every event; the bus gets it when someone listens.
			devices->dispatchEvent(event);
			if (auto currentContext = _contextManager.activeContext(event, action))
				_actions.publish(action);
			_events.publish(event);
		}
	}
CS_STRICT_SYSTEMS_END



	void InputDispatchSystem::onDetach() noexcept
	{
		_actions = {};
		_events = {};
		while (!_eventBuffer.empty()) (void)_eventBuffer.pop();
	}
	void InputDispatchSystem::shutdown() {}
}
