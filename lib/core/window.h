#ifndef __CSYREN_WINDOW__
#define __CSYREN_WINDOW__
#include <Windows.h>

namespace csyren::core
{
	class Window
	{
	public:
		Window(int width, int height, const wchar_t* name) noexcept;
		~Window() noexcept;
		Window(const Window&) = delete;
		Window& operator=(const Window&) = delete;


		HWND init() noexcept;

		const wchar_t* title() const noexcept { return _title; }
		HINSTANCE hInstance() const noexcept { return _hInst; }

	private:
		LRESULT handleMsg(HWND nWnd, UINT smg, WPARAM wParam, LPARAM lParam);
	
	private:
		static constexpr  const wchar_t* _wndCLassName = L"CSyrenWindowClass";
		HINSTANCE _hInst;
		int _width;
		int _height;
		const wchar_t* _title;
		HWND _hWnd;
	};
}



#endif
