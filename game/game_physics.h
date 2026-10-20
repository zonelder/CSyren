#pragma once
// Collision layers of this game. The engine only knows named layers and a
// collision matrix (physics::PhysicsLayers); which layers exist and how they
// interact is decided here. The enum is the single source of names: the
// registry takes them through reflection (defineEnum), components store them
// as CollisionLayer, and MaskOf<Layer> fields show up as checkbox lists in
// the inspector and as name lists in scene files.
#include "core/reflected_enum.h"
#include "core/entity.h"
#include "physics/physics_layers.h"
#include "physics/physics_query.h"

namespace csyren::game
{
	CS_ENUM(Layer, uint8_t,
		Default,     // level geometry, props, bots
		Player,      // the pilot capsule
		Projectile,  // grenades: never touch the player or each other
		Trigger)     // volumes only the player enters (reserved)

	using LayerMask = core::MaskOf<Layer>;

	namespace layers
	{
		inline physics::CollisionLayer of(Layer layer) { return physics::CollisionLayer::from(layer); }

		// What bullets, explosions and movement probes hit: level, props, bots.
		inline constexpr LayerMask world() { return LayerMask{ Layer::Default }; }

		inline physics::QueryFilter filter(LayerMask mask, core::Entity::ID ignore = core::Entity::invalidID)
		{
			physics::QueryFilter result;
			result.layerMask = physics::PhysicsLayers::toMask(mask);
			result.ignore = ignore;
			return result;
		}
		inline physics::QueryFilter worldFilter(core::Entity::ID ignore = core::Entity::invalidID) { return filter(world(), ignore); }

		// Registers the layers and the collision rules. Called once at startup,
		// before any scene creates physics bodies (GameSetupSystem::init).
		inline void registerAll(physics::PhysicsLayers& registry)
		{
			registry.defineEnum<Layer>();
			registry.setCollision(Layer::Player, Layer::Projectile, false);      // fired from inside the capsule
			registry.setCollision(Layer::Projectile, Layer::Projectile, false);  // grenades pass through each other
			registry.setCollision(Layer::Trigger, Layer::Default, false);
			registry.setCollision(Layer::Trigger, Layer::Projectile, false);
			registry.setCollision(Layer::Trigger, Layer::Trigger, false);
		}
	}
}
