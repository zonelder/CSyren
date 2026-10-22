#include "game_components.h"
#include "core/meta.h"
#include "core/meta_archive.h"

namespace csyren::game
{
	using namespace core::reflection;
	namespace
	{
		constexpr FieldFlags runtimeField = FieldFlags::EditorVisible | FieldFlags::Transient | FieldFlags::ReadOnly;
	}

	void PlayerTag::describe()
	{
		MetaFactory<PlayerTag>{}.type("PlayerTag"_hs)
			.data<&PlayerTag::version>("version"_hs, {}, FieldFlags::Serializable);
	}

	void PilotGround::describe()
	{
		MetaFactory<PilotGround>{}.type("PilotGround"_hs)
			.data<&PilotGround::runSpeed>("runSpeed"_hs)
			.data<&PilotGround::walkSpeed>("walkSpeed"_hs)
			.data<&PilotGround::crouchSpeed>("crouchSpeed"_hs)
			.data<&PilotGround::alwaysSprint>("alwaysSprint"_hs)
			.data<&PilotGround::accel>("accel"_hs)
			.data<&PilotGround::friction>("friction"_hs)
			.data<&PilotGround::stopSpeed>("stopSpeed"_hs);
	}
	void PilotAir::describe()
	{
		MetaFactory<PilotAir>{}.type("PilotAir"_hs)
			.data<&PilotAir::gravity>("gravity"_hs)
			.data<&PilotAir::strafeAccel>("strafeAccel"_hs)
			.data<&PilotAir::strafeSpeedCap>("strafeSpeedCap"_hs)
			.data<&PilotAir::control>("control"_hs)
			.data<&PilotAir::maxSpeed>("maxSpeed"_hs)
			.data<&PilotAir::terminalSpeed>("terminalSpeed"_hs);
	}
	void PilotJump::describe()
	{
		MetaFactory<PilotJump>{}.type("PilotJump"_hs)
			.data<&PilotJump::jumpSpeed>("jumpSpeed"_hs)
			.data<&PilotJump::doubleJumpSpeed>("doubleJumpSpeed"_hs)
			.data<&PilotJump::doubleJumpRedirect>("doubleJumpRedirect"_hs)
			.data<&PilotJump::doubleJumpMinSpeed>("doubleJumpMinSpeed"_hs)
			.data<&PilotJump::bufferTime>("bufferTime"_hs)
			.data<&PilotJump::coyoteTime>("coyoteTime"_hs)
			.data<&PilotJump::bhopWindow>("bhopWindow"_hs);
	}
	void PilotSlide::describe()
	{
		MetaFactory<PilotSlide>{}.type("PilotSlide"_hs)
			.data<&PilotSlide::minSpeed>("minSpeed"_hs)
			.data<&PilotSlide::boost>("boost"_hs)
			.data<&PilotSlide::maxSpeed>("maxSpeed"_hs)
			.data<&PilotSlide::friction>("friction"_hs)
			.data<&PilotSlide::slopeGain>("slopeGain"_hs)
			.data<&PilotSlide::steer>("steer"_hs)
			.data<&PilotSlide::exitSpeed>("exitSpeed"_hs)
			.data<&PilotSlide::crouchHeight>("crouchHeight"_hs)
			.data<&PilotSlide::standHeight>("standHeight"_hs);
	}
	void PilotWall::describe()
	{
		MetaFactory<PilotWall>{}.type("PilotWall"_hs)
			.data<&PilotWall::minSpeed>("minSpeed"_hs)
			.data<&PilotWall::speed>("speed"_hs)
			.data<&PilotWall::entryBoost>("entryBoost"_hs)
			.data<&PilotWall::entryUp>("entryUp"_hs)
			.data<&PilotWall::maxTime>("maxTime"_hs)
			.data<&PilotWall::gravityScale>("gravityScale"_hs)
			.data<&PilotWall::gravityRamp>("gravityRamp"_hs)
			.data<&PilotWall::maxFall>("maxFall"_hs)
			.data<&PilotWall::decay>("decay"_hs)
			.data<&PilotWall::stick>("stick"_hs)
			.data<&PilotWall::checkDistance>("checkDistance"_hs)
			.data<&PilotWall::maxEntryInto>("maxEntryInto"_hs)
			.data<&PilotWall::reattachDelay>("reattachDelay"_hs)
			.data<&PilotWall::jumpUp>("jumpUp"_hs)
			.data<&PilotWall::jumpAway>("jumpAway"_hs)
			.data<&PilotWall::jumpForward>("jumpForward"_hs)
			.data<&PilotWall::kickUp>("kickUp"_hs)
			.data<&PilotWall::kickAway>("kickAway"_hs)
			.data<&PilotWall::lookAwayExit>("lookAwayExit"_hs)
			.data<&PilotWall::cameraRoll>("cameraRoll"_hs);
	}
	void PilotMantle::describe()
	{
		MetaFactory<PilotMantle>{}.type("PilotMantle"_hs)
			.data<&PilotMantle::reach>("reach"_hs)
			.data<&PilotMantle::minHeight>("minHeight"_hs)
			.data<&PilotMantle::maxHeight>("maxHeight"_hs)
			.data<&PilotMantle::duration>("duration"_hs)
			.data<&PilotMantle::exitSpeed>("exitSpeed"_hs);
	}
	void PilotLook::describe()
	{
		MetaFactory<PilotLook>{}.type("PilotLook"_hs)
			.data<&PilotLook::sensitivity>("sensitivity"_hs)
			.data<&PilotLook::maxPitch>("maxPitch"_hs)
			.data<&PilotLook::baseFov>("baseFov"_hs)
			.data<&PilotLook::speedFovKick>("speedFovKick"_hs)
			.data<&PilotLook::eyeHeight>("eyeHeight"_hs)
			.data<&PilotLook::crouchEyeHeight>("crouchEyeHeight"_hs);
	}
	void PilotConfig::describe()
	{
		PilotGround::describe(); PilotAir::describe(); PilotJump::describe(); PilotSlide::describe();
		PilotWall::describe(); PilotMantle::describe(); PilotLook::describe();
		MetaFactory<PilotConfig>{}.type("PilotConfig"_hs)
			.data<&PilotConfig::ground>("ground"_hs)
			.data<&PilotConfig::air>("air"_hs)
			.data<&PilotConfig::jump>("jump"_hs)
			.data<&PilotConfig::slide>("slide"_hs)
			.data<&PilotConfig::wall>("wall"_hs)
			.data<&PilotConfig::mantle>("mantle"_hs)
			.data<&PilotConfig::look>("look"_hs);
	}

	void PilotState::describe()
	{
		MetaFactory<PilotState>{}.type("PilotState"_hs)
			.data<&PilotState::yaw>("yaw"_hs)
			.data<&PilotState::pitch>("pitch"_hs)
			.data<&PilotState::spawnPoint>("spawnPoint"_hs)
			.data<&PilotState::spawnYaw>("spawnYaw"_hs)
			.data<&PilotState::move>("move"_hs, {}, runtimeField)
			.data<&PilotState::speed>("speed"_hs, {}, runtimeField)
			.data<&PilotState::verticalSpeed>("verticalSpeed"_hs, {}, runtimeField)
			.data<&PilotState::stateTime>("stateTime"_hs, {}, runtimeField)
			.data<&PilotState::crouching>("crouching"_hs, {}, runtimeField)
			.data<&PilotState::sprinting>("sprinting"_hs, {}, runtimeField)
			.data<&PilotState::doubleJumpReady>("doubleJumpReady"_hs, {}, runtimeField)
			.data<&PilotState::wallNormal>("wallNormal"_hs, {}, runtimeField)
			.data<&PilotState::wallTime>("wallTime"_hs, {}, runtimeField);
	}

	void WeaponDef::describe()
	{
		MetaFactory<WeaponDef>{}.type("WeaponDef"_hs)
			.data<&WeaponDef::name>("name"_hs)
			.data<&WeaponDef::kind>("kind"_hs)
			.data<&WeaponDef::automatic>("automatic"_hs)
			.data<&WeaponDef::damage>("damage"_hs)
			.data<&WeaponDef::rpm>("rpm"_hs)
			.data<&WeaponDef::magazine>("magazine"_hs)
			.data<&WeaponDef::reloadTime>("reloadTime"_hs)
			.data<&WeaponDef::spread>("spread"_hs)
			.data<&WeaponDef::spreadMoving>("spreadMoving"_hs)
			.data<&WeaponDef::recoilPitch>("recoilPitch"_hs)
			.data<&WeaponDef::recoilYaw>("recoilYaw"_hs)
			.data<&WeaponDef::range>("range"_hs)
			.data<&WeaponDef::projectileSpeed>("projectileSpeed"_hs)
			.data<&WeaponDef::projectileRadius>("projectileRadius"_hs)
			.data<&WeaponDef::projectileLifetime>("projectileLifetime"_hs)
			.data<&WeaponDef::projectileGravity>("projectileGravity"_hs)
			.data<&WeaponDef::explosionRadius>("explosionRadius"_hs)
			.data<&WeaponDef::explosionImpulse>("explosionImpulse"_hs)
			.data<&WeaponDef::reloadClip>("reloadClip"_hs)
			.data<&WeaponDef::hitLayers>("hitLayers"_hs)
			.data<&WeaponDef::projectilePrefab>("projectilePrefab"_hs)
			.data<&WeaponDef::impactPrefab>("impactPrefab"_hs)
			.data<&WeaponDef::muzzleFlashPrefab>("muzzleFlashPrefab"_hs);
	}
	void WeaponInventory::describe()
	{
		WeaponDef::describe();
		MetaFactory<WeaponInventory>{}.type("WeaponInventory"_hs)
			.data<&WeaponInventory::primary>("primary"_hs)
			.data<&WeaponInventory::secondary>("secondary"_hs)
			.data<&WeaponInventory::active>("active"_hs)
			.data<&WeaponInventory::ammoPrimary>("ammoPrimary"_hs, {}, runtimeField)
			.data<&WeaponInventory::ammoSecondary>("ammoSecondary"_hs, {}, runtimeField)
			.data<&WeaponInventory::cooldown>("cooldown"_hs, {}, runtimeField)
			.data<&WeaponInventory::reloading>("reloading"_hs, {}, runtimeField);
	}
	void Projectile::describe()
	{
		MetaFactory<Projectile>{}.type("Projectile"_hs)
			.data<&Projectile::damage>("damage"_hs)
			.data<&Projectile::explosionRadius>("explosionRadius"_hs)
			.data<&Projectile::impulse>("impulse"_hs)
			.data<&Projectile::lifetime>("lifetime"_hs)
			.data<&Projectile::armTime>("armTime"_hs)
			.data<&Projectile::hitLayers>("hitLayers"_hs)
			.data<&Projectile::trail>("trail"_hs)
			.data<&Projectile::explosion>("explosion"_hs);
	}
	void Health::describe()
	{
		MetaFactory<Health>{}.type("Health"_hs)
			.data<&Health::max>("max"_hs)
			.data<&Health::current>("current"_hs)
			.data<&Health::respawnTime>("respawnTime"_hs)
			.data<&Health::dead>("dead"_hs, {}, runtimeField)
			.data<&Health::respawnTimer>("respawnTimer"_hs, {}, runtimeField);
	}
	void Bot::describe()
	{
		MetaFactory<Bot>{}.type("Bot"_hs)
			.data<&Bot::behaviour>("behaviour"_hs)
			.data<&Bot::speed>("speed"_hs)
			.data<&Bot::patrolOffset>("patrolOffset"_hs)
			.data<&Bot::chaseRange>("chaseRange"_hs)
			.data<&Bot::stopDistance>("stopDistance"_hs)
			.data<&Bot::spawn>("spawn"_hs, {}, runtimeField)
			.data<&Bot::phase>("phase"_hs, {}, runtimeField);
	}
	void Lifetime::describe()
	{
		MetaFactory<Lifetime>{}.type("Lifetime"_hs)
			.data<&Lifetime::remaining>("remaining"_hs);
	}

	void ViewModel::describe()
	{
		MetaFactory<ViewModel>{}.type("ViewModel"_hs)
			.data<&ViewModel::offset>("offset"_hs)
			.data<&ViewModel::swayPosition>("swayPosition"_hs)
			.data<&ViewModel::swayRotation>("swayRotation"_hs)
			.data<&ViewModel::swaySmoothing>("swaySmoothing"_hs)
			.data<&ViewModel::bobAmount>("bobAmount"_hs)
			.data<&ViewModel::bobFrequency>("bobFrequency"_hs)
			.data<&ViewModel::recoilBack>("recoilBack"_hs)
			.data<&ViewModel::recoilUp>("recoilUp"_hs)
			.data<&ViewModel::switchTime>("switchTime"_hs)
			.data<&ViewModel::switchDrop>("switchDrop"_hs)
			.data<&ViewModel::reloadDrop>("reloadDrop"_hs)
			.data<&ViewModel::reloadTilt>("reloadTilt"_hs)
			.data<&ViewModel::slideTilt>("slideTilt"_hs);
	}
	void ViewModelSlot::describe()
	{
		MetaFactory<ViewModelSlot>{}.type("ViewModelSlot"_hs)
			.data<&ViewModelSlot::slot>("slot"_hs)
			.data<&ViewModelSlot::muzzleNode>("muzzleNode"_hs);
	}
}
