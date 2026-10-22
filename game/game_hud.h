#pragma once
// Debug HUD: GameHudSystem copies gameplay state into the GameHud service
// (plain data) at the end of simulation; the editor draws it as an overlay on
// top of the Game viewport (crosshair, speed, movement state, ammo, health).
#include <algorithm>
#include <cstdio>
#include <string>
#include "core/typed_system.h"
#include "physics/character_controller.h"
#include "imGui/imgui.h"

#include "game_components.h"
#include "player_input.h"
#include "editor_overlays.h"
#include "editor_viewport_state.h"

CS_STRICT_SYSTEMS_BEGIN
namespace csyren::game
{
	struct GameHud
	{
		bool active = false;
		bool captured = false;
		float speed = 0.0f, verticalSpeed = 0.0f;
		PilotMove move = PilotMove::Air;
		bool grounded = false, doubleJump = false, crouching = false;
		int wallSide = 0;
		float wallTime = 0.0f;
		std::string weapon;
		int ammo = 0, magazine = 0;
		bool reloading = false;
		float hitMarker = 0.0f;
		float health = 0.0f;
		int botsAlive = 0, botsTotal = 0;
		bool sprinting = false;
	};

	class GameHudSystem : public core::TypedSystem<GameHudSystem>
	{
	public:
		static constexpr auto phase = "simulation";
		static constexpr auto after = "LifetimeSystem";

		void init() override
		{
			if (!core::Services::has<editor::EditorOverlays>()) return;
			core::Services::get<editor::EditorOverlays>()->set("GameHud", [] { drawOverlay(); });
		}
		void shutdown() override
		{
			if (core::Services::has<editor::EditorOverlays>()) core::Services::get<editor::EditorOverlays>()->remove("GameHud");
		}

		void execute(core::Select<PilotState>::Where<PlayerTag> players,
			core::Select<physics::CharacterController, WeaponInventory, Health> playerDetails,
			core::Select<Bot, Health> bots,
			core::Res<GameHud> hudService, core::Res<const PlayerInput> input)
		{
			auto& hud = *hudService;
			hud.active = false;
			hud.captured = input->captured;
			for (auto [ent, state] : players)
			{
				hud.active = true;
				hud.speed = state.speed; hud.verticalSpeed = state.verticalSpeed; hud.move = state.move;
				hud.doubleJump = state.doubleJumpReady; hud.crouching = state.crouching; hud.sprinting = state.sprinting;
				hud.wallSide = state.move == PilotMove::WallRun ? state.wallSide : 0; hud.wallTime = state.wallTime;
				if (auto cc = playerDetails.get<physics::CharacterController>(ent)) hud.grounded = cc->grounded;
				if (auto inventory = playerDetails.get<WeaponInventory>(ent))
				{
					const auto& weapon = inventory->active == 0 ? inventory->primary : inventory->secondary;
					hud.weapon = weapon.name; hud.magazine = weapon.magazine;
					hud.ammo = inventory->active == 0 ? inventory->ammoPrimary : inventory->ammoSecondary;
					hud.reloading = inventory->reloading; hud.hitMarker = inventory->hitMarker;
				}
				if (auto health = playerDetails.get<Health>(ent)) hud.health = health->current;
				break;
			}
			hud.botsAlive = hud.botsTotal = 0;
			for (auto [ent, bot, health] : bots) { ++hud.botsTotal; if (!health.dead) ++hud.botsAlive; }
		}

	protected:
		void onDetach() noexcept override
		{
			if (core::Services::has<GameHud>()) core::Services::get<GameHud>()->active = false;
		}

	private:
		static void drawOverlay()
		{
			if (!core::Services::has<GameHud>() || !core::Services::has<editor::EditorViewportState>()) return;
			const auto& hud = *core::Services::get<GameHud>();
			const auto& view = *core::Services::get<editor::EditorViewportState>();
			const auto& game = view.gameViewport;
			if (!hud.active || !game.visible) return;
			auto* draw = ImGui::GetForegroundDrawList();
			const ImVec2 center(game.x + game.width * 0.5f, game.y + game.height * 0.5f);

			// Crosshair.
			const ImU32 crossColor = hud.hitMarker > 0.0f ? IM_COL32(255, 80, 80, 255) : IM_COL32(255, 255, 255, 220);
			const float gap = 4.0f, length = 9.0f;
			draw->AddLine(ImVec2(center.x - gap - length, center.y), ImVec2(center.x - gap, center.y), crossColor, 2.0f);
			draw->AddLine(ImVec2(center.x + gap, center.y), ImVec2(center.x + gap + length, center.y), crossColor, 2.0f);
			draw->AddLine(ImVec2(center.x, center.y - gap - length), ImVec2(center.x, center.y - gap), crossColor, 2.0f);
			draw->AddLine(ImVec2(center.x, center.y + gap), ImVec2(center.x, center.y + gap + length), crossColor, 2.0f);
			if (hud.hitMarker > 0.0f)
				for (int sx : { -1, 1 }) for (int sy : { -1, 1 })
					draw->AddLine(ImVec2(center.x + sx * 6.0f, center.y + sy * 6.0f), ImVec2(center.x + sx * 14.0f, center.y + sy * 14.0f), crossColor, 2.0f);

			char buffer[256];
			float y = game.y + 10.0f;
			const float x = game.x + 12.0f;
			auto line = [&](ImU32 color, const char* text) {
				draw->AddText(ImVec2(x + 1, y + 1), IM_COL32(0, 0, 0, 200), text);
				draw->AddText(ImVec2(x, y), color, text);
				y += ImGui::GetTextLineHeight() + 2.0f;
			};
			std::snprintf(buffer, sizeof(buffer), "speed %5.1f m/s   vertical %+5.1f", hud.speed, hud.verticalSpeed);
			line(IM_COL32(255, 255, 255, 255), buffer);
			std::snprintf(buffer, sizeof(buffer), "state %s%s%s%s", std::string(core::enumName(hud.move)).c_str(),
				hud.crouching ? "  [crouch]" : hud.sprinting ? "  [sprint]" : "", hud.wallSide < 0 ? "  wall L" : hud.wallSide > 0 ? "  wall R" : "", "");
			line(IM_COL32(170, 230, 255, 255), buffer);
			std::snprintf(buffer, sizeof(buffer), "grounded %s   double jump %s   wall time %.2f", hud.grounded ? "yes" : "no",
				hud.doubleJump ? "ready" : "used", hud.wallTime);
			line(IM_COL32(200, 200, 200, 255), buffer);
			std::snprintf(buffer, sizeof(buffer), "%s  %d / %d%s", hud.weapon.c_str(), hud.ammo, hud.magazine, hud.reloading ? "  RELOADING" : "");
			line(IM_COL32(255, 220, 120, 255), buffer);
			std::snprintf(buffer, sizeof(buffer), "bots %d / %d alive", hud.botsAlive, hud.botsTotal);
			line(IM_COL32(255, 160, 160, 255), buffer);
			if (!hud.captured)
				line(IM_COL32(255, 255, 120, 255), "click the Game view to capture the mouse (Esc releases)");
			line(IM_COL32(150, 150, 150, 255), "WASD move  Shift sprint  Space jump/double jump  Ctrl/C crouch/slide  LMB fire  R reload  Q/1/2 weapon  F5 respawn");
		}
	};

	REGISTER_SYSTEM(GameHudSystem)
}
CS_STRICT_SYSTEMS_END
