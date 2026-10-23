#ifndef __CSYREN_EVENT_DISPATCHER__
#define __CSYREN_EVENT_DISPATCHER__
#include "input_context.h"
#include "devices.h"
#include "event_bus.h"
#include "typed_system.h"
#include "dispatch_handle.h"

#include <unordered_map>
#include "registration.h"

//todo make class a sender of
// InputEvent
// ActionEvent++
CS_STRICT_SYSTEMS_BEGIN
namespace csyren::core
{
	class InputDispatchSystem : public TypedSystem<InputDispatchSystem>, private details::DispatchHandle
	{
	public:
		static constexpr auto phase = "input";
		// Drains the window's input queue (filled on the main thread).
		static constexpr TaskAffinity affinity = TaskAffinity::MainThread;

		void init() override;

		void shutdown() override;

		// Updates the device snapshots and publishes input events on the scene bus.
		void execute(Res<Devices> devices);
	protected:
		void onAttach() override;
		void onDetach() noexcept override;

	private:
		InputContextManager _contextManager;

		Broadcast<InputAction> _actions;
		Broadcast<InputEvent> _events;

	};

	CS_SYSTEM(InputDispatchSystem);
}
CS_STRICT_SYSTEMS_END


#endif
