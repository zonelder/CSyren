#include "pch.h"
#include "window.h"
#include "input_enums.h"

#include <assert.h>


csyren::core::Window::Window(int width, int height, const wchar_t* name) noexcept :
	_hInst(GetModuleHandle(nullptr)),
	_width(width),
	_height(height),
	_title(name),
	_hWnd(nullptr)
{
}

csyren::core::Window::~Window() noexcept
{
	UnregisterClass(_wndCLassName, hInstance());
}

HWND csyren::core::Window::init() noexcept
{
	if (_hWnd)
		return _hWnd;

	WNDCLASSEX wcex;
	wcex.cbSize = sizeof(WNDCLASSEX);
	wcex.style = CS_HREDRAW | CS_VREDRAW;
	wcex.cbClsExtra = 0;
	wcex.cbWndExtra = 0;

	wcex.hCursor = nullptr;
	wcex.hbrBackground = nullptr;
	wcex.hIcon = nullptr;
	wcex.hIconSm = nullptr;

	wcex.lpszClassName = _wndCLassName;
	wcex.lpszMenuName = nullptr;
	wcex.hInstance = _hInst;

	wcex.lpfnWndProc = handleMsg;

	if (!RegisterClassEx(&wcex))
	{
		return _hWnd;
	}

	RECT wr{ 0 };
	wr.left = 100;
	wr.right = _width + wr.left;
	wr.top = 100;
	wr.bottom = _height + wr.top;
	if (AdjustWindowRect(&wr, WS_CAPTION | WS_MINIMIZEBOX | WS_SYSMENU, FALSE) == 0) {
		return _hWnd;
	}

	_hWnd = CreateWindow(_wndCLassName, _title,
		WS_CAPTION | WS_MINIMIZEBOX | WS_SYSMENU,
		CW_USEDEFAULT, CW_USEDEFAULT, wr.right - wr.left, wr.bottom - wr.top,
		nullptr, nullptr, _hInst, this
	);
	return _hWnd;
}

// Static message handler (called by OS)
LRESULT CALLBACK csyren::core::Window::handleMsg(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
	Window* pThis = nullptr;

	if (msg == WM_NCCREATE) 
	{
		CREATESTRUCT* pCreate = reinterpret_cast<CREATESTRUCT*>(lParam);
		pThis = reinterpret_cast<Window*>(pCreate->lpCreateParams);
		SetWindowLongPtr(hWnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(pThis));
		return DefWindowProc(hWnd, msg, wParam, lParam);
	}
	else 
	{
		pThis = reinterpret_cast<Window*>(GetWindowLongPtr(hWnd, GWLP_USERDATA));
	}

	if (pThis && pThis->ready()) 
	{
		return pThis->handleMsgImpl(hWnd, msg, wParam, lParam);
	}
	return DefWindowProc(hWnd, msg, wParam, lParam);
}

LRESULT csyren::core::Window::handleMsgImpl(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
	assert(_dispatcher != nullptr && "dispatcher should be ready in main game loop.");
	using InputEvent = input::InputEvent;
	using EventType = InputEvent::Type;
	using MouseButton = input::MouseButton;
	InputEvent event;
	switch (msg)
	{
	case WM_DESTROY:
		PostQuitMessage(0);
	case WM_CLOSE:
		PostQuitMessage(0);
		break;
	case WM_KEYDOWN:
	case WM_SYSKEYDOWN:
		
		event.type = (lParam & 0x40000000) ?
			EventType::Keyhold :
			EventType::KeyDown;
		event.code = static_cast<int>(wParam);
		event.data.keyboard.shift = _shiftDown;
		event.data.keyboard.ctrl = _ctrlDown;
		event.data.keyboard.alt = _altDown;
		_dispatcher->dispatch(event);
		break;

	case WM_KEYUP:
	case WM_SYSKEYUP:
		event.type = EventType::KeyUp;
		event.code = static_cast<int>(wParam);
		event.data.keyboard.shift = _shiftDown;
		event.data.keyboard.ctrl = _ctrlDown;
		event.data.keyboard.alt = _altDown;
		_dispatcher->dispatch(event);
		break;
	case WM_LBUTTONDOWN:
	case WM_RBUTTONDOWN:
	{
		const POINTS pt = MAKEPOINTS(lParam);
		event.type = EventType::MouseButtonDown;
		event.code = static_cast<int>((msg == WM_RBUTTONDOWN) ? MouseButton::Right : MouseButton::Left);
		event.data.mouse.x = pt.x;
		event.data.mouse.y = pt.y;
		_dispatcher->dispatch(event);
		break;
	}

	case WM_LBUTTONUP:
	case WM_RBUTTONUP:
	{
		const POINTS pt = MAKEPOINTS(lParam);
		event.type = EventType::MouseButtonUp;
		event.code = static_cast<int>((msg == WM_RBUTTONUP) ? MouseButton::Right : MouseButton::Left);
		event.data.mouse.x = pt.x;
		event.data.mouse.y = pt.y;
		_dispatcher->dispatch(event);
		break;
	}

	case WM_MOUSEWHEEL: 
	{
		const POINTS pt = MAKEPOINTS(lParam);
		event.type = EventType::MouseWheel;
		event.code = static_cast<int>(MouseButton::Middle);
		event.data.mouse.x = pt.x;
		event.data.mouse.y = pt.y;
		event.data.mouse.wheelDelta = GET_WHEEL_DELTA_WPARAM(wParam);
		_dispatcher->dispatch(event);
		break;
	}
	case WM_MOUSEMOVE:
	{
		const POINTS pt = MAKEPOINTS(lParam);
		event.type = EventType::MouseMove;
		event.code = static_cast<int>(MouseButton::UnknownButton);
		event.data.mouse.x = pt.x;
		event.data.mouse.y = pt.y;
		_dispatcher->dispatch(event);
		break;
	}

	}
	return DefWindowProc(hWnd, msg, wParam, lParam);
}
