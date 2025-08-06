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
	class DamageSystem
	{
	public:
		DamageSystem(csyren::core::events::EventBus2& bus) : _bus(bus)
		{
			_updateToken = bus.subscribe<core::events::UpdateEvent>(
				[this](const core::events::UpdateEvent& event)
				{
					update(event);
				});
		}
		~DamageSystem()
		{
			_bus.unsubscribe(_updateToken);
		}
		void update(const core::events::UpdateEvent& event)
		{
			event.scene.view<DamageComponent, DamageableComponent>().each(
				[&](csyren::core::Entity::ID id, DamageComponent& damage, DamageableComponent& damageable)
				{
					damageable.currentHealth -= damage.damage;
					if (damageable.currentHealth <= 0.0f)
					{
						event.scene.destroyEntity(id);
					}
					// Remove the damage component after applying it
					event.scene.removeComponent<DamageComponent>(id);
				}
			);
		}
	private:
		csyren::core::events::EventBus2& _bus;
		csyren::core::events::SubscriberToken _updateToken;
	};
}