#pragma once
// Target dummies: idle / patrol / chase behaviours on kinematic bodies, hit
// flash by swapping the material, death + respawn at the spawn point.
#include <algorithm>
#include <cmath>
#include "core/system_base.h"
#include "core/services.h"
#include "core/scene.h"
#include "core/transform.h"
#include "core/time.h"
#include "math/math.h"
#include "../mesh_filter.h"

#include "game_components.h"
#include "game_materials.h"

namespace csyren::game
{
	class BotSystem : public core::System
	{
	public:
		core::TaskDescription describe() const override
		{
			core::TaskDescription d; d.phase = "simulation"; d.after = { "ProjectileSystem" };
			d.write<Bot>().write<Health>().write<core::Transform>().write<render::MeshRenderer>().read<PlayerTag>();
			d.readResource<core::Time>().readResource<core::SceneEntities>();
			return d;
		}

		void execute() override
		{
			using namespace math;
			const auto* time = core::Services::get<const core::Time>();
			const float dt = std::clamp(time->deltaTime(), 0.0f, 0.05f);
			auto& scene = this->scene();

			Vector3 playerPos; bool hasPlayer = false;
			for (auto [ent, tag, tr] : scene.view<const PlayerTag, const core::Transform>()) { playerPos = tr.position; hasPlayer = true; break; }

			for (auto [ent, bot, health, tr] : scene.view<Bot, Health, core::Transform>())
			{
				if (!bot.spawnCaptured) { bot.spawn = tr.position; bot.spawnCaptured = true; }
				auto renderer = scene.getComponent<render::MeshRenderer>(ent);
				if (renderer && !bot.baseMaterial) bot.baseMaterial = renderer->material;

				if (health.dead)
				{
					health.respawnTimer -= dt;
					// Park the body far below the arena while dead.
					const Vector3 parked = bot.spawn + Vector3(0.0f, -200.0f, 0.0f);
					if (!tr.position.exactEqual(parked)) { tr.position = parked; scene.patch<core::Transform>(ent); }
					if (health.respawnTimer <= 0.0f)
					{
						health.dead = false; health.current = health.max; health.flash = 0.0f;
						tr.position = bot.spawn; bot.phase = 0.0f; scene.patch<core::Transform>(ent);
					}
					continue;
				}

				// Hit flash.
				health.flash = (std::max)(0.0f, health.flash - dt);
				if (renderer)
				{
					const bool flash = health.flash > 0.0f;
					if (flash != bot.flashing)
					{
						bot.flashing = flash;
						renderer->material = flash ? _hitMaterial : bot.baseMaterial;
					}
				}

				Vector3 target = tr.position;
				switch (bot.behaviour)
				{
				case BotBehaviour::Patrol:
				{
					const float length = bot.patrolOffset.magnitude();
					if (length < 1e-3f) break;
					bot.phase += dt * bot.speed / length;
					const float t = 0.5f - 0.5f * std::cos(bot.phase * DirectX::XM_PI);
					target = bot.spawn + bot.patrolOffset * t;
					break;
				}
				case BotBehaviour::Chase:
				{
					if (!hasPlayer) break;
					Vector3 toPlayer = playerPos - tr.position; toPlayer.y = 0.0f;
					const float distance = toPlayer.magnitude();
					if (distance > bot.chaseRange || distance < bot.stopDistance) break;
					target = tr.position + toPlayer / distance * (bot.speed * dt);
					break;
				}
				default: break;
				}
				if (!tr.position.exactEqual(target)) { tr.position = target; scene.patch<core::Transform>(ent); }
			}
		}

	protected:
		void onAttach() override { _hitMaterial = materials::get(materials::BotHit); }

	private:
		render::MaterialHandle _hitMaterial;
	};

	REGISTER_SYSTEM(BotSystem)

	class LifetimeSystem : public core::System
	{
	public:
		core::TaskDescription describe() const override
		{
			core::TaskDescription d; d.phase = "simulation"; d.after = { "BotSystem" };
			d.structuralChanges = true;
			d.write<Lifetime>().readResource<core::Time>();
			return d;
		}
		void execute() override
		{
			const auto* time = core::Services::get<const core::Time>();
			const float dt = std::clamp(time->deltaTime(), 0.0f, 0.05f);
			auto& scene = this->scene();
			for (auto [ent, life] : scene.view<Lifetime>())
			{
				life.remaining -= dt;
				if (life.remaining <= 0.0f) scene.destroyEntity(ent);
			}
		}
	};

	REGISTER_SYSTEM(LifetimeSystem)
}
