#pragma once
#include "core/context.h"
#include "core/event_bus.h"
#include "core/scene.h"
#include "core/time.h"
#include "space_shooter_components.h"
#include "transform.h"


using namespace csyren::components::demo::space_shooter;

namespace csyren::demo::space_shooter
{
	class MoveSystem
	{
	public:
		MoveSystem(csyren::core::events::EventBus2& bus) : _bus(bus)
		{
			_updateToken = bus.subscribe<core::events::UpdateEvent>(
				[this](const core::events::UpdateEvent& event)
				{
					update(event);
				});
		}
		~MoveSystem()
		{
			_bus.unsubscribe(_updateToken);
		}

		void update(const core::events::UpdateEvent& event)
		{

			event.scene.view<components::Transform,VelocityComponent>().each(
				[&](csyren::core::Entity::ID id, components::Transform& transform, VelocityComponent& velocity)
				{
					auto deltaTime = event.time.deltaTime();
					transform.pos.x += velocity.x * deltaTime;
					transform.pos.y += velocity.y * deltaTime;
				});
		}

	private:
		csyren::core::events::EventBus2& _bus;
		csyren::core::events::SubscriberToken _updateToken;
	};
}