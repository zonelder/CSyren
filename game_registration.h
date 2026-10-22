#pragma once
// Transitional registry entries for game/ (the movement prototype).
//
// game/ keeps its old registration style until it moves to its own project:
// its systems register themselves with REGISTER_SYSTEM once their headers are
// compiled into the executable, and its components and services are listed
// here as markers for tools/csyren_registry.py (CSyren.vcxproj adds this file
// to the scan roots). Remove this header when game/ moves out and gets CS_*
// markers of its own.
#include "core/registration.h"
#include "game/game_components.h"
#include "game/player_input.h"
#include "game/game_materials.h"
#include "game/player_input_system.h"
#include "game/pilot_movement_system.h"
#include "game/camera_rig_system.h"
#include "game/view_model_system.h"
#include "game/weapon_system.h"
#include "game/projectile_system.h"
#include "game/bot_system.h"
#include "game/game_hud.h"

namespace csyren::game
{
    CS_COMPONENT(PlayerTag);
    CS_COMPONENT(PilotConfig);
    CS_COMPONENT(PilotState);
    CS_COMPONENT(WeaponInventory);
    CS_COMPONENT(Projectile);
    CS_COMPONENT(Health);
    CS_COMPONENT(Bot);
    CS_COMPONENT(Lifetime);
    CS_COMPONENT(ViewModel);
    CS_COMPONENT(ViewModelSlot);

    CS_SERVICE(PlayerInput);
    CS_SERVICE(GameHud);
    CS_SERVICE(ViewModelState);
}
