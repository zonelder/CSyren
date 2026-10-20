#pragma once
// Collects gameplay input: keyboard state from Devices plus a locked-cursor
// mouse look. The cursor is captured by clicking the game view and released
// with Escape, when the window loses focus or when the system detaches.
#include "cstdmf/window_clean.h"
#include "core/system_base.h"
#include "core/services.h"
#include "core/devices.h"
#include "core/window.h"
#include "core/input_enums.h"

#include "player_input.h"
#include "../editor_viewport_state.h"

namespace csyren::game
{
	class PlayerInputSystem : public core::System
	{
	public:
		core::TaskDescription describe() const override
		{
			core::TaskDescription d; d.phase = "input"; d.affinity = core::TaskAffinity::MainThread;
			d.after = { "InputDispatchSystem" };
			d.dynamicResources = true;
			return d;
		}

		void execute() override
		{
			auto& in = *core::Services::get<PlayerInput>();
			const auto* devices = core::Services::get<const core::Devices>();
			const auto* window = core::Services::get<const core::Window>();
			const auto& keyboard = devices->keyboard();
			const auto& mouse = devices->mouse();
			const HWND hwnd = window->hwnd();
			const bool foreground = GetForegroundWindow() == hwnd;

			RECT client{}; GetClientRect(hwnd, &client);
			POINT cursor{}; GetCursorPos(&cursor); ScreenToClient(hwnd, &cursor);
			float centerX = float(client.right - client.left) * 0.5f, centerY = float(client.bottom - client.top) * 0.5f;
			bool overGame = cursor.x >= client.left && cursor.x < client.right && cursor.y >= client.top && cursor.y < client.bottom;
			if (core::Services::has<editor::EditorViewportState>())
			{
				const auto& view = *core::Services::get<const editor::EditorViewportState>();
				if (view.viewportInput)
				{
					const auto& game = view.gameViewport;
					overGame = game.contains(float(cursor.x), float(cursor.y));
					if (game.visible) { centerX = game.x + game.width * 0.5f; centerY = game.y + game.height * 0.5f; }
				}
			}
			_center = POINT{ LONG(centerX), LONG(centerY) };
			ClientToScreen(hwnd, &_center);

			const bool leftButton = mouse.isButtonDown(core::MouseButton::Left);
			bool justCaptured = false;
			if (!in.captured)
			{
				if (leftButton && !in.prevLeftButton && overGame && foreground) { capture(hwnd); justCaptured = true; }
			}
			else if (!foreground || keyboard.isKeyDown(core::KeyCode::Escape))
			{
				release();
			}
			in.prevLeftButton = leftButton;
			in.captured = _captured;

			in.lookDX = in.lookDY = 0.0f;
			if (_captured)
			{
				POINT now{}; GetCursorPos(&now);
				if (!justCaptured) { in.lookDX = float(now.x - _center.x); in.lookDY = float(now.y - _center.y); }
				SetCursorPos(_center.x, _center.y);
				RECT clip{}; GetClientRect(hwnd, &clip);
				POINT topLeft{ clip.left, clip.top }, bottomRight{ clip.right, clip.bottom };
				ClientToScreen(hwnd, &topLeft); ClientToScreen(hwnd, &bottomRight);
				RECT screenClip{ topLeft.x, topLeft.y, bottomRight.x, bottomRight.y };
				ClipCursor(&screenClip);
			}

			const bool active = _captured;
			auto key = [&](int code) { return active && keyboard.isKeyDown(code); };
			auto keyCode = [&](core::KeyCode code) { return key(static_cast<int>(code)); };
			in.moveX = (keyCode(core::KeyCode::D) ? 1.0f : 0.0f) - (keyCode(core::KeyCode::A) ? 1.0f : 0.0f);
			in.moveZ = (keyCode(core::KeyCode::W) ? 1.0f : 0.0f) - (keyCode(core::KeyCode::S) ? 1.0f : 0.0f);

			const bool jump = keyCode(core::KeyCode::Space);
			const bool crouch = key(VK_CONTROL) || keyCode(core::KeyCode::C);
			in.sprintHeld = key(VK_SHIFT);
			const bool fire = active && leftButton && !justCaptured;
			const bool reload = keyCode(core::KeyCode::R);
			const bool swap = keyCode(core::KeyCode::Q);
			const bool slot0 = keyCode(core::KeyCode::Num1);
			const bool slot1 = keyCode(core::KeyCode::Num2);
			const bool reset = keyCode(core::KeyCode::F5);

			in.jumpHeld = jump;     in.jumpPressed = jump && !in.prevJump;
			in.crouchHeld = crouch; in.crouchPressed = crouch && !in.prevCrouch;
			in.fireHeld = fire;     in.firePressed = fire && !in.prevFire;
			in.reloadPressed = reload && !in.prevReload;
			in.swapPressed = swap && !in.prevSwap;
			in.selectWeapon = (slot0 && !in.prevSlot0) ? 0 : (slot1 && !in.prevSlot1) ? 1 : -1;
			in.resetPressed = reset && !in.prevReset;

			in.prevJump = jump; in.prevCrouch = crouch; in.prevFire = fire; in.prevReload = reload;
			in.prevSwap = swap; in.prevSlot0 = slot0; in.prevSlot1 = slot1; in.prevReset = reset;
		}

	protected:
		void onDetach() noexcept override
		{
			release();
			if (core::Services::has<PlayerInput>()) *core::Services::get<PlayerInput>() = PlayerInput{};
		}

	private:
		void capture(HWND hwnd)
		{
			if (_captured) return;
			_captured = true;
			SetCursorPos(_center.x, _center.y);
			while (ShowCursor(FALSE) >= 0) {}
			(void)hwnd;
		}
		void release() noexcept
		{
			if (!_captured) return;
			_captured = false;
			ClipCursor(nullptr);
			while (ShowCursor(TRUE) < 0) {}
		}
		bool _captured = false;
		POINT _center{};
	};

	REGISTER_SYSTEM(PlayerInputSystem)
}
