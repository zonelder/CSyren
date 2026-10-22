#pragma once
// Flat-colored procedural materials for the prototype. They are registered
// by name so scene files can reference them like the engine primitives.
#include <array>
#include <string>
#include "core/system_base.h"
#include "core/services.h"
#include "dx12_graphic/primitives.h"
#include "dx12_graphic/resource_manager.h"
#include "physics/physics_layers.h"
#include "game_physics.h"

namespace csyren::game
{
	namespace materials
	{
		constexpr const char* Ground     = "__game_mat_ground__";
		constexpr const char* Wall       = "__game_mat_wall__";
		constexpr const char* WallAlt    = "__game_mat_wall_alt__";
		constexpr const char* Platform   = "__game_mat_platform__";
		constexpr const char* Ramp       = "__game_mat_ramp__";
		constexpr const char* Accent     = "__game_mat_accent__";
		constexpr const char* Bot        = "__game_mat_bot__";
		constexpr const char* BotHit     = "__game_mat_bot_hit__";
		constexpr const char* Projectile = "__game_mat_projectile__";
		constexpr const char* Impact     = "__game_mat_impact__";
		constexpr const char* Explosion  = "__game_mat_explosion__";
		constexpr const char* Metal      = "__game_mat_metal__";
		constexpr const char* Lamp       = "__game_mat_lamp__";
		constexpr const char* Glass      = "__game_mat_glass__";

		// PBR flat materials (deferred lit shader) unless `transparent`, which
		// uses the forward lit shader with alpha blending.
		struct Entry { const char* name; float r, g, b, a; float roughness, metallic, emissive; bool transparent; };
		inline const std::array<Entry, 14>& table()
		{
			static const std::array<Entry, 14> entries{ {
				{ Ground,     0.30f, 0.32f, 0.36f, 1.0f, 0.90f, 0.00f, 0.0f, false },
				{ Wall,       0.62f, 0.60f, 0.55f, 1.0f, 0.80f, 0.00f, 0.0f, false },
				{ WallAlt,    0.50f, 0.56f, 0.64f, 1.0f, 0.55f, 0.00f, 0.0f, false },
				{ Platform,   0.42f, 0.47f, 0.40f, 1.0f, 0.70f, 0.00f, 0.0f, false },
				{ Ramp,       0.56f, 0.44f, 0.36f, 1.0f, 0.60f, 0.00f, 0.0f, false },
				{ Accent,     0.95f, 0.55f, 0.15f, 1.0f, 0.40f, 0.00f, 0.6f, false },
				{ Bot,        0.85f, 0.20f, 0.20f, 1.0f, 0.50f, 0.00f, 0.0f, false },
				{ BotHit,     1.00f, 0.95f, 0.80f, 1.0f, 0.50f, 0.00f, 3.0f, false },
				{ Projectile, 0.20f, 0.90f, 1.00f, 1.0f, 0.30f, 0.00f, 4.0f, false },
				{ Impact,     1.00f, 0.90f, 0.30f, 1.0f, 0.50f, 0.00f, 6.0f, false },
				{ Explosion,  1.00f, 0.45f, 0.10f, 1.0f, 0.50f, 0.00f, 8.0f, false },
				{ Metal,      0.80f, 0.82f, 0.85f, 1.0f, 0.25f, 1.00f, 0.0f, false },
				{ Lamp,       1.00f, 0.85f, 0.60f, 1.0f, 0.60f, 0.00f, 12.0f, false },
				{ Glass,      0.55f, 0.80f, 1.00f, 0.3f, 0.12f, 0.00f, 0.0f, true },
			} };
			return entries;
		}

		inline void registerAll()
		{
			auto& rm = *core::Services::get<render::ResourceManager>();
			for (const auto& entry : table())
			{
				const std::string name = entry.name;
				const Entry data = entry;
				rm.registerProcedural<render::Material>(name, [name, data](render::ResourceManager& manager) {
					const auto shader = data.transparent ? render::Primitives::getForwardLitShader() : render::Primitives::getLitShader();
					const render::MaterialStateDesc states = data.transparent ? render::Primitives::transparentStates() : render::MaterialStateDesc{};
					const auto handle = manager.createMaterial(name, shader, states);
					if (auto* material = manager.getMaterial(handle))
					{
						render::Primitives::initializeMaterialDefaults(*material);
						material->setVector("tint", DirectX::XMFLOAT4(data.r, data.g, data.b, data.a));
						material->setFloat("roughness", data.roughness);
						material->setFloat("metallic", data.metallic);
						material->setVector("emissive", DirectX::XMFLOAT4(data.r, data.g, data.b, data.emissive));
					}
					return handle;
				});
			}
		}

		inline render::MaterialHandle get(const char* name)
		{
			return core::Services::get<render::ResourceManager>()->get<render::Material>(name);
		}
	}

	// Registers the prototype's resources and collision layers before any scene
	// is loaded (init only).
	class GameSetupSystem : public core::OnceSystem
	{
	public:
		void init() override
		{
			materials::registerAll();
			layers::registerAll(*core::Services::get<physics::PhysicsLayers>());
		}
	};
	REGISTER_SYSTEM(GameSetupSystem)
}
