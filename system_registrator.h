#pragma once
#include "core/system_base.h"

//TODO(dx12) make rebuild event reflection that find all systems;

#include "core/input_dispatch_system.h"
#include "editor_camera_controller_system.h"
#include "physics/physic_system.h"
#include "physics/spring_join_system.h"
#include "mesh_render_system.h"
#include "scene_loader.h"
#include "dx12_graphic/start_render_system.h"

#include "render_environment_system.h"
#include "dx12_graphic/presentation_system.h"
#include "dx12_graphic/frame_prepare_system.h"

#include "core/scene_events_system.h"
#include "animation_system.h"

// Deferred + forward lighting pipeline.
#include "dx12_graphic/lighting/light_collect_system.h"
#include "dx12_graphic/lighting/light_cluster_system.h"
#include "dx12_graphic/lighting/light_pass_system.h"
#include "dx12_graphic/lighting/tonemap_system.h"
#include "forward_render_system.h"

// Titanfall-style movement / shooter prototype.
#include "game/game_materials.h"
#include "game/player_input_system.h"
#include "game/pilot_movement_system.h"
#include "game/camera_rig_system.h"
#include "game/view_model_system.h"
#include "game/weapon_system.h"
#include "game/projectile_system.h"
#include "game/bot_system.h"
#include "game/game_hud.h"
