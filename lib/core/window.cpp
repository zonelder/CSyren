#include "pch.h"
#include "window.h"

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

	wcex.lpfnWndProc = DefWindowProc;

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
}

LRESULT csyren::core::Window::handleMsg(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
	switch (msg)
	{
	case WM_DESTROY:
		PostQuitMessage(0);
	case WM_CLOSE:
		PostQuitMessage(0);
		break;
	}
	return DefWindowProc(hWnd, msg, wParam, lParam);
}
