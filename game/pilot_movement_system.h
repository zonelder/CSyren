#pragma once
// Titanfall-style pilot movement: Source-like ground/air acceleration, bunny
// hopping, double jump with momentum redirect, slide + slide hop, wallrun with
// wall jump / wall kick, and ledge mantling. The system produces a desired
// velocity for the CharacterController; the physics system moves the capsule.
#include <algorithm>
#include <cmath>
#include "core/typed_system.h"
#include "core/transform.h"
#include "core/transform_hierarchy.h"
#include "core/camera.h"
#include "core/time.h"
#include "math/math.h"
#include "physics/physic_system.h"
#include "physics/character_controller.h"

#include "game_components.h"
#include "game_physics.h"
#include "player_input.h"

CS_STRICT_SYSTEMS_BEGIN
namespace csyren::game
{
	class PilotMovementSystem : public core::TypedSystem<PilotMovementSystem>
	{
	public:
		static constexpr auto phase = "simulation";
		static constexpr auto before = "PhysicsSystem";

		using Pilots = core::Update<const PilotConfig, PilotState, physics::CharacterController, core::Transform>;
		using Cameras = core::Select<core::Camera>;

		void execute(Pilots pilots, Cameras cameras, core::Res<const PlayerInput> input,
			core::Res<const core::Time> time, core::Res<const physics::PhysicsEngine> engine)
		{
			const float dt = std::clamp(time->deltaTime(), 0.0f, 0.05f);
			for (auto [ent, config, state, cc, tr] : pilots)
				update(pilots, cameras, *engine, *input, dt, ent, config, state, cc, tr);
		}

	private:
		static float horizontal(const math::Vector3& v) { return std::sqrt(v.x * v.x + v.z * v.z); }
		static math::Vector3 flat(const math::Vector3& v) { return { v.x, 0.0f, v.z }; }
		static float smoothstep(float t) { t = std::clamp(t, 0.0f, 1.0f); return t * t * (3.0f - 2.0f * t); }

		void update(const Pilots& pilots, const Cameras& cameras, const physics::PhysicsEngine& engine, const PlayerInput& in, float dt,
			core::Entity::ID ent, const PilotConfig& config, PilotState& state, physics::CharacterController& cc, core::Transform& tr)
		{
			using namespace math;
			const auto& g = config.ground; const auto& air = config.air; const auto& jump = config.jump;
			const auto& slide = config.slide; const auto& wall = config.wall; const auto& mantle = config.mantle;

			if (!state.initialized)
			{
				state.initialized = true;
				state.spawnPoint = tr.position; state.spawnYaw = state.yaw;
				state.cameraEntity = core::Entity::invalidID;
				if (const auto* entity = pilots.entities().try_get(ent))
					for (auto child : entity->children)
						if (cameras.contains(child)) { state.cameraEntity = child; break; }
				cc.height = slide.standHeight;
			}
			if (in.resetPressed || tr.position.y < -30.0f) // F5 or fell out of the level
			{
				tr.position = state.spawnPoint; state.yaw = state.spawnYaw; state.pitch = 0.0f;
				cc.velocity = Vector3::zero; state.move = PilotMove::Air; state.crouching = false;
				state.lastWallValid = false; state.doubleJumpReady = true;
				pilots.patch<core::Transform>(ent);
			}

			//---------------------------------------------------------------- look
			if (in.captured)
			{
				state.yaw += in.lookDX * config.look.sensitivity;
				state.pitch += in.lookDY * config.look.sensitivity;
			}
			state.pitch -= state.recoilPitch; state.yaw += state.recoilYaw;
			state.recoilPitch = state.recoilYaw = 0.0f;
			state.pitch = std::clamp(state.pitch, -config.look.maxPitch, config.look.maxPitch);
			while (state.yaw > 180.0f) state.yaw -= 360.0f;
			while (state.yaw < -180.0f) state.yaw += 360.0f;
			const Quaternion yawRotation = Quaternion::angleAxis(state.yaw, Vector3::up);
			if (!tr.rotation.exactEqual(yawRotation)) { tr.rotation = yawRotation; pilots.patch<core::Transform>(ent); }

			const Vector3 forward = flat(yawRotation * Vector3::forward).normalized();
			const Vector3 right = flat(yawRotation * Vector3::right).normalized();
			Vector3 wishDir = forward * in.moveZ + right * in.moveX;
			const bool hasWish = wishDir.sqrMagnitude() > 1e-4f;
			if (hasWish) wishDir.normalize();

			//---------------------------------------------------------------- timers
			state.stateTime += dt;
			state.timeSinceJump += dt;
			state.jumpBuffer = (std::max)(0.0f, state.jumpBuffer - dt);
			if (in.jumpPressed) state.jumpBuffer = jump.bufferTime;
			_wallReattach = (std::max)(0.0f, _wallReattach - dt);
			const bool grounded = cc.grounded;
			if (grounded) state.timeSinceGrounded = 0.0f; else state.timeSinceGrounded += dt;
			const bool justLanded = grounded && !state.wasGrounded;
			if (justLanded) _timeSinceLanded = 0.0f; else _timeSinceLanded += dt;

			Vector3 velocity = cc.velocity;
			Vector3 hv = flat(velocity);
			float vy = velocity.y;
			const Vector3 feet = core::worldMatrix(pilots, ent).translation();
			const float radius = cc.radius;
			const physics::QueryFilter worldFilter = layers::worldFilter();

			auto applyFriction = [&](Vector3& v, float friction, float stopSpeed) {
				const float speed = horizontal(v);
				if (speed < 1e-3f) { v = Vector3::zero; return; }
				const float control = (std::max)(speed, stopSpeed);
				const float drop = control * friction * dt;
				const float newSpeed = (std::max)(speed - drop, 0.0f);
				v *= newSpeed / speed;
			};
			auto accelerate = [&](Vector3& v, const Vector3& dir, float wishSpeed, float accel) {
				if (!hasWish) return;
				const float current = v.dot(dir);
				const float add = wishSpeed - current;
				if (add <= 0.0f) return;
				v += dir * (std::min)(accel * dt, add);
			};
			auto airStrafe = [&](Vector3& v, const Vector3& dir) {
				if (!hasWish) return;
				const float wishSpeed = g.runSpeed;
				const float capped = (std::min)(wishSpeed, air.strafeSpeedCap);
				const float current = v.dot(dir);
				const float add = capped - current;
				if (add > 0.0f) v += dir * (std::min)(air.strafeAccel * wishSpeed * dt, add);
				// Direct control: never gains speed above what we already had (or run speed).
				const float before = horizontal(v);
				v += dir * (air.control * dt);
				const float limit = (std::max)(before, g.runSpeed);
				const float after = horizontal(v);
				if (after > limit && after > 1e-4f) v *= limit / after;
			};
			auto canStand = [&]() {
				const float r = radius * 0.95f;
				const float distance = slide.standHeight - 2.0f * r - 0.04f;
				if (distance <= 0.0f) return true;
				return !engine.sphereCast(feet + Vector3::up * (r + 0.04f), r, Vector3::up, distance, worldFilter).hit;
			};
			auto setHeight = [&](bool crouch) { cc.height = crouch ? slide.crouchHeight : slide.standHeight; };
			struct WallProbe { bool found = false; Vector3 normal; Vector3 point; };
			auto probeWall = [&](const Vector3& dir, float distance) {
				WallProbe probe;
				const Vector3 origin = feet + Vector3::up * (cc.currentHeight * 0.55f);
				auto hit = engine.sphereCast(origin, 0.3f, dir, distance, worldFilter);
				if (!hit.hit) return probe;
				if (std::abs(hit.normal.y) > 0.35f) return probe;
				if (hit.normal.dot(dir) > -0.4f) return probe;
				probe.found = true; probe.normal = flat(hit.normal).normalized(); probe.point = hit.point;
				return probe;
			};
			auto doJump = [&](float speed) {
				vy = speed; state.move = PilotMove::Air; state.jumpBuffer = 0.0f;
				state.timeSinceJump = 0.0f; state.jumpedSinceGround = true; state.stateTime = 0.0f;
			};
			auto startSlide = [&]() {
				state.move = PilotMove::Slide; state.stateTime = 0.0f; state.crouching = true; setHeight(true);
				const float speed = horizontal(hv);
				if (speed > 1e-3f) hv *= (std::min)(speed + slide.boost, (std::max)(slide.maxSpeed, speed)) / speed;
			};
			auto wallTangent = [&](const Vector3& normal, const Vector3& preferred) {
				Vector3 tangent = Vector3::up.cross(normal);
				if (tangent.sqrMagnitude() < 1e-6f) tangent = forward;
				tangent.normalize();
				if (tangent.dot(preferred) < 0.0f) tangent = -tangent;
				return tangent;
			};
			auto startWallRun = [&](const WallProbe& probe, int side) {
				state.move = PilotMove::WallRun; state.stateTime = 0.0f;
				state.wallNormal = probe.normal; state.wallSide = side; state.wallTime = 0.0f;
				state.wallPoint = probe.point;
				state.jumpBuffer = 0.0f; // a press buffered on the way in must not jump off immediately
				const Vector3 preferred = horizontal(hv) > 0.5f ? hv.normalized() : forward;
				const Vector3 tangent = wallTangent(probe.normal, preferred);
				state.wallSpeed = (std::max)(horizontal(hv), wall.speed) + wall.entryBoost;
				hv = tangent * state.wallSpeed;
				vy = (std::max)(vy, wall.entryUp);
				state.doubleJumpReady = true;
				state.crouching = false; setHeight(false);
			};
			auto leaveWall = [&]() {
				state.lastWallNormal = state.wallNormal; state.lastWallPoint = state.wallPoint; state.lastWallValid = true;
				_wallReattach = wall.reattachDelay; state.cameraRollTarget = 0.0f;
				state.move = PilotMove::Air; state.stateTime = 0.0f;
			};
			// The same wall plane cannot be re-run until the ground or another wall is touched.
			auto isLastWall = [&](const WallProbe& probe) {
				if (!state.lastWallValid || probe.normal.dot(state.lastWallNormal) < 0.9f) return false;
				return std::abs((probe.point - state.lastWallPoint).dot(probe.normal)) < 1.0f;
			};
			auto tryWallRun = [&]() {
				if (_wallReattach > 0.0f || horizontal(hv) < wall.minSpeed) return false;
				const Vector3 moveDir = horizontal(hv) > 0.5f ? hv.normalized() : forward;
				WallProbe best; float bestDistance = 1e9f;
				auto consider = [&](const WallProbe& probe) {
					if (!probe.found || isLastWall(probe)) return;
					// Along the wall or into it at an angle (the entry redirects the speed); not moving away from it.
					const float into = -moveDir.dot(probe.normal);
					if (into > wall.maxEntryInto || into < -0.3f) return;
					const float distance = (probe.point - feet).sqrMagnitude();
					if (distance < bestDistance) { best = probe; bestDistance = distance; }
				};
				// 1. The capsule already touches a wall (any direction) - the Titanfall trigger.
				if (cc.wallContact && std::abs(cc.wallNormal.y) < 0.35f)
				{
					WallProbe contact; contact.found = true; contact.normal = flat(cc.wallNormal).normalized();
					contact.point = feet + Vector3::up * (cc.currentHeight * 0.55f) - contact.normal * radius;
					consider(contact);
				}
				// 2. Fan of casts around the movement direction and the facing.
				if (!best.found)
				{
					const Vector3 moveRight = Vector3::up.cross(moveDir).normalized();
					for (const Vector3& base : { moveRight, right })
						for (int side : { -1, 1 })
							for (float angle : { 0.0f, 40.0f, -40.0f })
							{
								const Vector3 dir = Quaternion::angleAxis(angle, Vector3::up) * (base * float(side));
								consider(probeWall(dir, wall.checkDistance));
							}
				}
				if (!best.found) return false;
				const int side = best.normal.dot(right) < 0.0f ? 1 : -1;
				startWallRun(best, side);
				return true;
			};
			auto tryMantle = [&]() {
				const float reach = radius + mantle.reach;
				const Vector3 chest = feet + Vector3::up * (cc.currentHeight * 0.5f);
				auto front = engine.raycast(chest, forward, reach, worldFilter);
				if (!front.hit || std::abs(front.normal.y) > 0.4f) return false;
				Vector3 top = front.point + forward * 0.35f;
				top.y = feet.y + mantle.maxHeight + 0.1f;
				auto down = engine.raycast(top, Vector3::down, mantle.maxHeight + 0.1f - mantle.minHeight, worldFilter);
				if (!down.hit || down.normal.y < 0.7f) return false;
				const float height = down.point.y - feet.y;
				if (height < mantle.minHeight || height > mantle.maxHeight) return false;
				const float r = radius * 0.9f;
				if (engine.sphereCast(down.point + Vector3::up * (r + 0.05f), r, Vector3::up, slide.standHeight - 2.0f * r, worldFilter).hit) return false;
				state.move = PilotMove::Mantle; state.stateTime = 0.0f; state.jumpBuffer = 0.0f;
				state.mantleStart = feet;
				state.mantleTarget = Vector3(down.point.x, down.point.y + 0.03f, down.point.z) + forward * (radius * 0.4f);
				state.mantleTime = 0.0f;
				state.cameraRollTarget = 0.0f;
				hv = Vector3::zero; vy = 0.0f;
				return true;
			};

			auto move = state.move;
			if (justLanded && move != PilotMove::Mantle)
			{
				state.landImpulse = (std::max)(0.0f, -vy);
				state.doubleJumpReady = true; state.lastWallValid = false; state.jumpedSinceGround = false;
				state.cameraRollTarget = 0.0f;
				if (move == PilotMove::WallRun) state.move = PilotMove::Air;
				move = state.move;
				if (move == PilotMove::Air)
				{
					vy = 0.0f;
					if (in.crouchHeld && horizontal(hv) > slide.minSpeed) startSlide();
					else { state.move = PilotMove::Ground; state.stateTime = 0.0f; }
					move = state.move;
				}
			}

			switch (move)
			{
			case PilotMove::Ground:
			{
				if (!grounded) { state.move = PilotMove::Air; state.stateTime = 0.0f; break; }
				if (in.crouchHeld)
				{
					if (!state.crouching && horizontal(hv) > slide.minSpeed) { startSlide(); break; }
					state.crouching = true;
				}
				else if (state.crouching && canStand()) state.crouching = false;
				setHeight(state.crouching);

				const bool hop = state.jumpBuffer > 0.0f || (in.jumpHeld && _timeSinceLanded < jump.bhopWindow);
				if (!hop) applyFriction(hv, g.friction, g.stopSpeed);
				const bool sprinting = in.sprintHeld || g.alwaysSprint;
				accelerate(hv, wishDir, state.crouching ? g.crouchSpeed : sprinting ? g.runSpeed : g.walkSpeed, g.accel);
				state.sprinting = sprinting && !state.crouching;
				vy = -1.0f; // keep contact on slopes/steps
				if (hop) doJump(jump.jumpSpeed);
				break;
			}
			case PilotMove::Slide:
			{
				if (!grounded) { state.move = PilotMove::Air; state.stateTime = 0.0f; break; }
				setHeight(true);
				applyFriction(hv, slide.friction, 0.5f);
				const Vector3 n = cc.groundNormal;
				const Vector3 gravityVector(0.0f, -air.gravity, 0.0f);
				const Vector3 projected = gravityVector - n * gravityVector.dot(n);
				hv += flat(projected) * (slide.slopeGain * dt);
				if (hasWish)
				{
					const float before = horizontal(hv);
					hv += wishDir * (slide.steer * dt);
					const float after = horizontal(hv);
					if (after > before && after > 1e-4f) hv *= before / after;
				}
				if (horizontal(hv) > slide.maxSpeed) hv *= slide.maxSpeed / horizontal(hv);
				vy = -1.0f;
				if (state.jumpBuffer > 0.0f) { doJump(jump.jumpSpeed); state.crouching = in.crouchHeld; break; }
				if (!in.crouchHeld && canStand()) { state.crouching = false; setHeight(false); state.move = PilotMove::Ground; state.stateTime = 0.0f; break; }
				if (horizontal(hv) < slide.exitSpeed) { state.move = PilotMove::Ground; state.stateTime = 0.0f; break; }
				break;
			}
			case PilotMove::Air:
			{
				state.crouching = in.crouchHeld;
				setHeight(state.crouching);
				// Touching any wall refreshes the jump kit, like in Titanfall.
				if (cc.wallContact && std::abs(cc.wallNormal.y) < 0.35f && _wallReattach <= 0.0f) state.doubleJumpReady = true;
				if (state.jumpBuffer > 0.0f)
				{
					if (!state.jumpedSinceGround && state.timeSinceGrounded < jump.coyoteTime)
					{
						doJump(jump.jumpSpeed);
					}
					else if (in.jumpPressed)
					{
						// Wall kick: jump off any wall we touch or face.
						WallProbe kick;
						if (cc.wallContact && std::abs(cc.wallNormal.y) < 0.35f) { kick.found = true; kick.normal = flat(cc.wallNormal).normalized(); }
						if (!kick.found && hasWish) kick = probeWall(wishDir, radius + 0.35f);
						if (!kick.found) kick = probeWall(forward, radius + 0.35f);
						if (kick.found)
						{
							hv = hv * 0.5f + kick.normal * wall.kickAway + (hasWish ? wishDir * 1.5f : Vector3::zero);
							doJump(wall.kickUp);
							state.doubleJumpReady = true;
							state.lastWallNormal = kick.normal; state.lastWallPoint = feet - kick.normal * radius; state.lastWallValid = true;
							_wallReattach = wall.reattachDelay;
						}
						else if (state.doubleJumpReady)
						{
							state.doubleJumpReady = false;
							if (hasWish)
							{
								const float speed = (std::max)(horizontal(hv), jump.doubleJumpMinSpeed);
								hv = Vector3::lerp(hv, wishDir * speed, jump.doubleJumpRedirect);
							}
							doJump(jump.doubleJumpSpeed);
						}
					}
				}
				vy -= air.gravity * dt;
				vy = (std::max)(vy, -air.terminalSpeed);
				airStrafe(hv, wishDir);
				if (horizontal(hv) > air.maxSpeed) hv *= air.maxSpeed / horizontal(hv);
				if (in.moveZ > 0.3f && vy < 4.0f && tryMantle()) break;
				if (tryWallRun()) break;
				break;
			}
			case PilotMove::WallRun:
			{
				state.wallTime += dt;
				const Vector3 origin = feet + Vector3::up * (cc.currentHeight * 0.55f);
				auto hit = engine.sphereCast(origin, 0.3f, -state.wallNormal, wall.checkDistance + 0.5f, worldFilter);
				const bool lost = !hit.hit || std::abs(hit.normal.y) > 0.4f || flat(hit.normal).normalized().dot(state.wallNormal) < 0.3f;
				if (lost || grounded || state.wallTime > wall.maxTime) { leaveWall(); if (grounded) { state.move = PilotMove::Ground; } break; }
				state.wallNormal = flat(hit.normal).normalized(); state.wallPoint = hit.point;
				const Vector3 preferred = horizontal(hv) > 0.5f ? hv.normalized() : forward;
				const Vector3 tangent = wallTangent(state.wallNormal, preferred);
				state.wallSpeed = (std::max)(state.wallSpeed - wall.decay * dt, wall.minSpeed);
				hv = tangent * state.wallSpeed;
				const float gravityScale = wall.gravityScale + (1.0f - wall.gravityScale) * std::clamp(state.wallTime / (std::max)(wall.gravityRamp, 0.01f), 0.0f, 1.0f);
				vy -= air.gravity * gravityScale * dt;
				vy = (std::max)(vy, -wall.maxFall);
				state.cameraRollTarget = float(state.wallSide) * wall.cameraRoll;

				const bool pushingAway = in.moveX * float(state.wallSide) < -0.5f;
				const bool lookingAway = forward.dot(state.wallNormal) > wall.lookAwayExit;
				if (in.jumpPressed || state.jumpBuffer > 0.0f)
				{
					hv = state.wallNormal * wall.jumpAway + tangent * (state.wallSpeed + wall.jumpForward);
					leaveWall();
					doJump(wall.jumpUp);
					break;
				}
				if (pushingAway || lookingAway) { leaveWall(); break; }
				if (in.moveZ > 0.3f && tryMantle()) { state.lastWallValid = false; break; }
				// Push into the wall so the capsule keeps contact.
				cc.velocity = hv + Vector3::up * vy - state.wallNormal * wall.stick;
				state.speed = horizontal(hv); state.verticalSpeed = vy; state.wasGrounded = grounded;
				return;
			}
			case PilotMove::Mantle:
			{
				state.mantleTime += dt;
				const float p = std::clamp(state.mantleTime / (std::max)(mantle.duration, 0.01f), 0.0f, 1.0f);
				const float pv = smoothstep(p / 0.65f);
				const float ph = smoothstep((p - 0.35f) / 0.65f);
				const Vector3 target = state.mantleTarget, start = state.mantleStart;
				const Vector3 pos(start.x + (target.x - start.x) * ph, start.y + (target.y - start.y) * pv, start.z + (target.z - start.z) * ph);
				tr.position = pos; pilots.patch<core::Transform>(ent);
				hv = Vector3::zero; vy = 0.0f;
				if (p >= 1.0f)
				{
					hv = forward * mantle.exitSpeed;
					state.move = PilotMove::Air; state.stateTime = 0.0f; state.jumpedSinceGround = false;
					state.doubleJumpReady = true; state.lastWallValid = false;
				}
				break;
			}
			}

			cc.velocity = hv + Vector3::up * vy;
			state.speed = horizontal(hv);
			state.verticalSpeed = vy;
			state.wasGrounded = grounded;
		}

		float _wallReattach = 0.0f;
		float _timeSinceLanded = 10.0f;
	};

	REGISTER_SYSTEM(PilotMovementSystem)
}
CS_STRICT_SYSTEMS_END
