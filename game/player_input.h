#pragma once
// Per-frame gameplay input snapshot (service). Filled by PlayerInputSystem in
// the "input" phase and read by gameplay systems in "simulation".
namespace csyren::game
{
	struct PlayerInput
	{
		bool captured = false;     // mouse is locked to the game view
		float moveX = 0.0f;        // -1 left .. +1 right
		float moveZ = 0.0f;        // -1 back .. +1 forward
		float lookDX = 0.0f;       // pixels this frame, positive right
		float lookDY = 0.0f;       // pixels this frame, positive down
		bool jumpHeld = false, jumpPressed = false;
		bool crouchHeld = false, crouchPressed = false;
		bool sprintHeld = false;
		bool fireHeld = false, firePressed = false;
		bool reloadPressed = false;
		bool swapPressed = false;
		int selectWeapon = -1;     // 0/1 when a slot key was pressed
		bool resetPressed = false; // respawn at the spawn point
		// state used by PlayerInputSystem for edge detection
		bool prevJump = false, prevCrouch = false, prevFire = false, prevReload = false, prevSwap = false, prevSlot0 = false, prevSlot1 = false, prevReset = false, prevLeftButton = false;
	};
}
