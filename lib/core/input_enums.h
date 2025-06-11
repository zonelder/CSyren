#ifndef __CSYREN_KEYCODE__
#define __CSYREN_KEYCODE__
#include <Windows.h>

namespace csyren::core::input
{
	enum class KeyCode : int
	{
		// Alphanumeric keys

		A = 'A',
		B = 'B',
		C = 'C',
		D = 'D',
		E = 'E',
		F = 'F',
		G = 'G',
		H = 'H',
		I = 'I',
		J = 'J',
		K = 'K',
		L = 'L',
		M = 'M',
		N = 'N',
		O = 'O',
		P = 'P',
		Q = 'Q',
		R = 'R',
		S = 'S',
		T = 'T',
		U = 'U',
		V = 'V',
		W = 'W',
		X = 'X',
		Y = 'Y',
		Z = 'Z',
		Num0 = '0',
		Num1 = '1',
		Num2 = '2',
		Num3 = '3',
		Num4 = '4',
		Num5 = '5',
		Num6 = '6',
		Num7 = '7',
		Num8 = '8',
		Num9 = '9',

		// Function keys
		F1 = VK_F1,
		F2 = VK_F2,
		F3 = VK_F3,
		F4 = VK_F4,
		F5 = VK_F5,
		F6 = VK_F6,
		F7 = VK_F7,
		F8 = VK_F8,
		F9 = VK_F9,
		F10 = VK_F10,
		F11 = VK_F11,
		F12 = VK_F12,

		// Special keys
		Space =  VK_SPACE,
		Escape = VK_ESCAPE,
		Tab = VK_TAB,
		CapsLock = VK_CAPITAL,
		LShift = VK_LSHIFT,
		RShift = VK_RSHIFT,
		LControl = VK_LCONTROL,
		RControl = VK_RCONTROL,
		LAlt = VK_LMENU,
		RAlt = VK_RMENU,
		Enter = VK_RETURN,
		Backspace = VK_BACK,
		Delete = VK_DELETE,
		Insert = VK_INSERT,
		Home = VK_HOME,
		End = VK_END,
		PageUp = VK_PRIOR,
		PageDown = VK_NEXT,
		ArrowUp = VK_UP,
		ArrowDown = VK_DOWN,
		ArrowLeft = VK_LEFT,
		ArrowRight = VK_RIGHT,

		// Numpad keys
		Numpad0 =			VK_NUMPAD0,
		Numpad1 =			VK_NUMPAD1,
		Numpad2 =			VK_NUMPAD2,
		Numpad3 =			VK_NUMPAD3,
		Numpad4 =			VK_NUMPAD4,
		Numpad5 =			VK_NUMPAD5,
		Numpad6 =			VK_NUMPAD6,
		Numpad7 =			VK_NUMPAD7,
		Numpad8 =			VK_NUMPAD8,
		Numpad9 =			VK_NUMPAD9,
		NumpadDivide =		VK_DIVIDE,
		NumpadMultiply =	VK_MULTIPLY,
		NumpadSubtract =	VK_SUBTRACT,
		NumpadAdd =			VK_ADD,
		NumpadDecimal =		VK_DECIMAL,

		// Unknown
		UnknownKey = 0xFF,
	};

	enum class MouseButton
	{
		Left = 0x01,
		Right,
		Middle,
		Button4,
		Button5,
		Count,
		UnknownButton = 0xFF,
	};

	enum class GamepadButton
	{
		A = 0x01,
		B,
		X,
		Y,
		DPadUp,
		DPadDown,
		DPadLeft,
		DPadRight,
		Start,
		Back,
		L1,
		R1,
		L2,
		R2,
		LThumb,
		RThumb,
		UnknownButton = 0xFF,
	};

	enum class GamepadAxis
	{
		LeftX = 0x01,
		LeftY,
		RightX,
		RightY,
		LeftTrigger,
		RightTrigger,
		UnknownAxis = 0xFF,
	};
}

#endif
