#pragma once

// PlayerInputSystem is responsible for handling player input and updating the game state accordingly.

#include "core/event_bus.h"
#include "core/input_event.h"
#include "core/scene.h"
#include "core/scene_events.h"
#include "core/context.h"
#include "core/space_shooter_components.h"
#include "core/transform.h"
#include "mesh_filter.h"
#include "dx12_graphic/primitives.h"
#include "core/entity.h"


namespace csyren::demo::space_shooter
{
	class PlayerInputSystem
	{
	public:
		PlayerInputSystem(core::events::EventBus2& bus,core::Scene& scene)
			: _bus(bus)
		{
			// Register input event handlers
			_bus.subscribe<core::input::InputEvent>([&](const core::input::InputEvent& event) 
				{
				handleInput(event, scene);
				});


			_bus.subscribe<core::events::UpdateEvent>([&](core::events::UpdateEvent& event)
				{
					update(event);
				});
		}

		void handleInput(const core::input::InputEvent& event, core::Scene& scene)
		{
			if (event.type == core::input::InputEvent::Type::KeyDown)
			{
				switch (static_cast<core::input::KeyCode>(event.code))
				{
				case core::input::KeyCode::W:
					axisInputY += 1.0f; // Handle move forward
					break;
				case core::input::KeyCode::S:
					axisInputY -= 1.0f; // Handle move backward
					break;
				case core::input::KeyCode::A:
					axisInputY -= 1.0f; // Handle move left
					break;
				case core::input::KeyCode::D:
					axisInputY += 1.0f;
					// Handle move right
					break;
				case core::input::KeyCode::Space:
					shootInput = true;
					break;
				default:
					break;
				}
			}
			else if (event.type == core::input::InputEvent::Type::KeyUp)
			{
				switch (static_cast<core::input::KeyCode>(event.code))
				{
				case core::input::KeyCode::W:
					axisInputY -= 1.0f; // Handle move forward
					break;
				case core::input::KeyCode::S:
					axisInputY += 1.0f; // Handle move backward
					break;
				case core::input::KeyCode::A:
					axisInputY += 1.0f; // Handle move left
					break;
				case core::input::KeyCode::D:
					axisInputY -= 1.0f;
					// Handle move right
					break;
				case core::input::KeyCode::Space:
					shootInput = false;
					break;
				default:
					break;
				}
			}
		}


		void update(core::events::UpdateEvent& event)
		{
			//for all players components in scene if it has velocity component we should change velocity with input.if it has shoot component we should create a projectile
			event.scene.view<components::Transform,components::demo::space_shooter::PlayerComponent, components::demo::space_shooter::VelocityComponent>().each([&](core::Entity::ID id, components::Transform& transform, components::demo::space_shooter::PlayerComponent& player, components::demo::space_shooter::VelocityComponent& velocity)
				{
					// Update player velocity based on input
					velocity.x = axisInputX * 0.5f;
					velocity.y = axisInputY * 0.5f;
					if (shootInput)
					{
						// Create a projectile entity
						auto projectile = event.scene.createEntity();
						auto projectileTransform = event.scene.addComponent<components::Transform>(projectile);
						auto projectileVelocity = event.scene.addComponent<components::demo::space_shooter::VelocityComponent>(projectile);
						auto projectileMesh = event.scene.addComponent<components::MeshFilter>(projectile);
						auto projectileRenderer = event.scene.addComponent<components::MeshRenderer>(projectile);
						projectileTransform->pos = transform.pos; // Set position to player's position
						projectileTransform->pos.y += 0.2f;
						projectileVelocity->x = 0.0f;
						projectileVelocity->x = 1.0f; // Set initial velocity
						projectileMesh->mesh = render::Primitives::getQuad(event.resources);
						projectileRenderer->material = render::Primitives::getDefaultMaterial(event.resources);
					}
				});
		}

	private:


		float axisInputX, axisInputY;
		bool shootInput;
		core::events::EventBus2& _bus;

	};

}