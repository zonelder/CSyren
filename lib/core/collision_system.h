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
	class CollisionSystem
	{
	public:
		CollisionSystem(csyren::core::events::EventBus2& bus) : _bus(bus)
		{
			_updateToken = bus.subscribe<core::events::UpdateEvent>(
				[this](const core::events::UpdateEvent& event)
				{
					update(event);
				});
		}
		~CollisionSystem()
		{
			_bus.unsubscribe(_updateToken);
		}
		void update(const core::events::UpdateEvent& event)
		{
			auto projectileView = event.scene.view<CircleColliderComponent, components::Transform, ProjectileComponent>();
			auto damagableView = event.scene.view<CircleColliderComponent, components::Transform, DamageableComponent>();
			projectileView.each(
				[&](csyren::core::Entity::ID projectile_id, CircleColliderComponent& projectile_collider, components::Transform& projectile_transform, ProjectileComponent& projectile)
				{
					damagableView.each(
						[&](csyren::core::Entity::ID damagable_id, CircleColliderComponent& damagable_collider, components::Transform& damagable_transform, DamageableComponent& damagable)
						{
							if (projectile_id == damagable_id) return; // Skip self collision
							// Simple circle-circle collision detection
							float dx = projectile_transform.pos.x - damagable_transform.pos.x;
							float dy = projectile_transform.pos.y - damagable_transform.pos.y;
							float distanceSquared = dx * dx + dy * dy;
							float radiusSum = projectile_collider.radius + damagable_collider.radius;

							if (distanceSquared < radiusSum * radiusSum)
							{
								// hit was done. add damage to target
								event.scene.addComponent<DamageComponent>(damagable_id, DamageComponent{ projectile.damage });
							}
						});
				});
		}
	private:
		csyren::core::events::EventBus2& _bus;
		csyren::core::events::SubscriberToken _updateToken;
	};
}

