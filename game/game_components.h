#pragma once
// Gameplay components for the Titanfall-style movement/shooter prototype.
// Only float/int/bool/string/Vector3 fields are reflected; runtime-only data
// (entity ids, resource handles) is deliberately left out of describe().
#include <string>
#include <cstdint>
#include "core/entity.h"
#include "core/prefab.h"
#include "math/math.h"
#include "dx12_graphic/resource_handle.h"
#include "core/reflected_enum.h"
#include "game_physics.h"

namespace csyren::game
{
	//---------------------------------------------------------------- player
	struct PlayerTag
	{
		int version = 1; // reflected placeholder so the component has a body in the archive
		static void describe();
	};

	struct PilotGround
	{
		float runSpeed = 8.0f;        // m/s, sprint (Shift)
		float walkSpeed = 5.0f;       // m/s, without Shift
		float crouchSpeed = 3.5f;
		bool alwaysSprint = false;    // Titanfall "auto sprint": Shift not needed
		float accel = 70.0f;          // m/s^2
		float friction = 7.0f;        // Source-style friction coefficient
		float stopSpeed = 1.5f;
		static void describe();
	};
	struct PilotAir
	{
		float gravity = 20.0f;        // m/s^2
		float strafeAccel = 14.0f;    // Quake/Source air accelerate (per wish speed)
		float strafeSpeedCap = 0.9f;  // m/s, wish speed cap for air strafing
		float control = 9.0f;         // m/s^2, direct air control (never gains speed)
		float maxSpeed = 22.0f;       // horizontal clamp
		float terminalSpeed = 45.0f;
		static void describe();
	};
	struct PilotJump
	{
		float jumpSpeed = 6.8f;
		float doubleJumpSpeed = 6.4f;
		float doubleJumpRedirect = 0.85f; // how strongly double jump re-aims momentum to the input direction
		float doubleJumpMinSpeed = 7.0f;  // horizontal speed granted by a double jump with input
		float bufferTime = 0.12f;
		float coyoteTime = 0.10f;
		float bhopWindow = 0.10f;         // jump held within this time after landing keeps all momentum
		static void describe();
	};
	struct PilotSlide
	{
		float minSpeed = 5.6f;        // above walk speed: slides need a sprint
		float boost = 3.5f;
		float maxSpeed = 15.0f;
		float friction = 1.8f;
		float slopeGain = 1.4f;       // multiplier of gravity projected onto the slope
		float steer = 4.0f;           // m/s^2
		float exitSpeed = 3.0f;
		float crouchHeight = 1.0f;
		float standHeight = 1.8f;
		static void describe();
	};
	struct PilotWall
	{
		float minSpeed = 4.0f;
		float speed = 9.5f;           // minimum running speed along the wall
		float entryBoost = 1.0f;
		float entryUp = 2.0f;         // small upward arc at the start
		float maxTime = 2.2f;
		float gravityScale = 0.15f;   // gravity at the start of the run
		float gravityRamp = 1.6f;     // seconds until full gravity
		float maxFall = 7.0f;
		float decay = 0.8f;           // speed lost per second while running
		float stick = 2.5f;           // velocity into the wall to keep contact
		float checkDistance = 1.2f;   // sphere cast distance from the capsule center
		float maxEntryInto = 0.97f;   // max dot(moveDir, -wallNormal) still allowed to attach (speed is redirected along the wall)
		float reattachDelay = 0.12f;  // seconds after leaving a wall before another can be grabbed
		float jumpUp = 6.5f;
		float jumpAway = 7.0f;
		float jumpForward = 1.5f;
		float kickUp = 5.5f;          // wall kick (jump off a wall without wallrunning)
		float kickAway = 6.0f;
		float lookAwayExit = 0.55f;   // dot(forward, wallNormal) above this detaches
		float cameraRoll = 12.0f;     // degrees
		static void describe();
	};
	struct PilotMantle
	{
		float reach = 1.0f;
		float minHeight = 0.6f;
		float maxHeight = 2.3f;
		float duration = 0.32f;
		float exitSpeed = 4.5f;
		static void describe();
	};
	struct PilotLook
	{
		float sensitivity = 0.11f;    // degrees per pixel
		float maxPitch = 89.0f;
		float baseFov = 68.0f;        // vertical degrees
		float speedFovKick = 7.0f;
		float eyeHeight = 1.62f;
		float crouchEyeHeight = 0.95f;
		static void describe();
	};

	struct PilotConfig
	{
		PilotGround ground;
		PilotAir air;
		PilotJump jump;
		PilotSlide slide;
		PilotWall wall;
		PilotMantle mantle;
		PilotLook look;
		static void describe();
	};

	CS_ENUM(PilotMove, int, Ground, Air, Slide, WallRun, Mantle)

	struct PilotState
	{
		PilotMove move = PilotMove::Air;
		float yaw = 0.0f;            // degrees, positive turns right
		float pitch = 0.0f;          // degrees, positive looks down
		float speed = 0.0f;          // horizontal m/s (readout)
		float verticalSpeed = 0.0f;
		float stateTime = 0.0f;
		bool crouching = false;
		bool sprinting = false;
		bool doubleJumpReady = true;
		bool wasGrounded = false;
		float jumpBuffer = 0.0f;
		float timeSinceGrounded = 0.0f;
		float timeSinceJump = 0.0f;
		bool jumpedSinceGround = false;
		// wall running
		math::Vector3 wallNormal = math::Vector3::zero;
		int wallSide = 0;            // -1 left, +1 right
		float wallTime = 0.0f;
		float wallSpeed = 0.0f;
		math::Vector3 wallPoint = math::Vector3::zero;
		math::Vector3 lastWallNormal = math::Vector3::zero;
		math::Vector3 lastWallPoint = math::Vector3::zero;
		bool lastWallValid = false;
		// mantle
		math::Vector3 mantleStart = math::Vector3::zero;
		math::Vector3 mantleTarget = math::Vector3::zero;
		float mantleTime = 0.0f;
		// camera feedback (consumed by CameraRigSystem)
		float cameraRollTarget = 0.0f;
		float landImpulse = 0.0f;    // vertical speed absorbed on the last landing
		float recoilPitch = 0.0f;    // pending recoil kick (degrees)
		float recoilYaw = 0.0f;
		math::Vector3 spawnPoint = math::Vector3::zero;
		float spawnYaw = 0.0f;
		// runtime only (not reflected)
		core::Entity::ID cameraEntity = core::Entity::invalidID;
		bool initialized = false;
		static void describe();
	};

	//---------------------------------------------------------------- weapons
	CS_ENUM(WeaponKind, int, Hitscan, Projectile)

	struct WeaponDef
	{
		std::string name = "Rifle";
		WeaponKind kind = WeaponKind::Hitscan;
		bool automatic = true;
		float damage = 25.0f;
		float rpm = 700.0f;
		int magazine = 30;
		float reloadTime = 1.6f;
		float spread = 0.6f;          // degrees, standing
		float spreadMoving = 1.4f;    // degrees, airborne / sliding
		float recoilPitch = 0.35f;    // degrees per shot
		float recoilYaw = 0.25f;      // random +-
		float range = 300.0f;
		float projectileSpeed = 32.0f;
		float projectileRadius = 0.13f;
		float projectileLifetime = 5.0f;
		bool projectileGravity = true;
		float explosionRadius = 3.0f;
		float explosionImpulse = 6.0f;
		std::string reloadClip;       // e.g. "models/rifle.glb#anim:reload"; its length overrides reloadTime
		LayerMask hitLayers = layers::world(); // what hitscan rays / explosions hit
		// Spawned copies (empty: none). The projectile gets the weapon's tuning above.
		core::PrefabRef projectilePrefab{ "prefabs/grenade.prefab" };
		core::PrefabRef impactPrefab{ "prefabs/impact.prefab" };
		core::PrefabRef muzzleFlashPrefab{ "prefabs/muzzle_flash.prefab" };
		static void describe();
	};

	struct WeaponInventory
	{
		WeaponDef primary;
		WeaponDef secondary;
		int active = 0;
		int ammoPrimary = -1;         // -1 = fill from magazine on first update
		int ammoSecondary = -1;
		float cooldown = 0.0f;
		float reloadTimer = 0.0f;
		bool reloading = false;
		float hitMarker = 0.0f;       // seconds left to show the hit marker
		float viewKick = 0.0f;        // visual recoil for the camera rig
		static void describe();
	};

	struct Projectile
	{
		float trailTimer = 0.0f;      // not reflected
		float damage = 60.0f;
		float explosionRadius = 3.0f;
		float impulse = 6.0f;
		float lifetime = 5.0f;
		float armTime = 0.03f;        // ignore contacts right after spawning
		bool exploded = false;
		LayerMask hitLayers = layers::world(); // copied from the weapon
		core::Entity::ID owner = core::Entity::invalidID; // not reflected
		core::PrefabRef trail{ "prefabs/projectile_trail.prefab" };   // spawned along the flight (empty: none)
		core::PrefabRef explosion{ "prefabs/explosion.prefab" };      // spawned on detonation (empty: none)
		static void describe();
	};

	//---------------------------------------------------------------- targets
	struct Health
	{
		float max = 100.0f;
		float current = 100.0f;
		bool dead = false;
		float respawnTime = 3.0f;
		float respawnTimer = 0.0f;
		float flash = 0.0f;           // hit flash seconds left
		static void describe();
	};

	CS_ENUM(BotBehaviour, int, Idle, Patrol, Chase)
	struct Bot
	{
		BotBehaviour behaviour = BotBehaviour::Patrol;
		float speed = 3.0f;
		math::Vector3 patrolOffset{ 6.0f, 0.0f, 0.0f };
		float chaseRange = 25.0f;
		float stopDistance = 2.5f;
		// runtime
		math::Vector3 spawn = math::Vector3::zero;
		float phase = 0.0f;
		bool spawnCaptured = false;        // not reflected
		render::MaterialHandle baseMaterial; // not reflected
		bool flashing = false;             // not reflected
		static void describe();
	};

	struct Lifetime
	{
		float remaining = 1.0f;
		static void describe();
	};

	//---------------------------------------------------------------- first-person view model
	// Root entity under the pilot camera that carries the weapon models.
	struct ViewModel
	{
		math::Vector3 offset{ 0.24f, -0.22f, 0.42f };
		float swayPosition = 0.0025f;  // meters per pixel of mouse movement
		float swayRotation = 0.06f;    // degrees per pixel
		float swaySmoothing = 11.0f;
		float bobAmount = 0.014f;
		float bobFrequency = 9.5f;     // steps per second at run speed
		float recoilBack = 0.05f;      // meters at full view kick
		float recoilUp = 4.0f;         // degrees at full view kick
		float switchTime = 0.28f;
		float switchDrop = 0.22f;
		float reloadDrop = 0.06f;
		float reloadTilt = 12.0f;
		float slideTilt = 6.0f;
		// runtime (not reflected)
		int shownSlot = -1;
		int switchTarget = 0;
		float switchTimer = 0.0f;
		float swayX = 0.0f, swayY = 0.0f, swayYaw = 0.0f, swayPitch = 0.0f;
		float bobPhase = 0.0f, bobWeight = 0.0f;
		static void describe();
	};

	// Weapon model root (one per inventory slot) under the ViewModel root.
	struct ViewModelSlot
	{
		int slot = 0;
		std::string muzzleNode = "muzzle";
		// runtime (not reflected)
		math::Vector3 baseScale{ 1.0f, 1.0f, 1.0f };
		bool baseScaleCaptured = false;
		core::Entity::ID muzzle = core::Entity::invalidID;
		uint64_t muzzleRevision = 0;
		static void describe();
	};

	// Service written by ViewModelSystem for the weapon code (muzzle placement).
	struct ViewModelState
	{
		bool hasMuzzle = false;
		math::Vector3 muzzlePosition = math::Vector3::zero;
		math::Vector3 muzzleForward = math::Vector3::forward;
		bool switching = false;
		core::Entity::ID activeSlotEntity = core::Entity::invalidID;
	};
}
