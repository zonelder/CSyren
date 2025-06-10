#ifndef __CSYREN_WINDOW__
#define __CSYREN_WINDOW__
#include <Windows.h>
#include  "input_dispatcher.h"

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
		void setInputDispatcher(input::InputDispatcher* dispatcher) { _dispatcher = dispatcher; }
		const wchar_t* title() const noexcept { return _title; }
		HINSTANCE hInstance() const noexcept { return _hInst; }


		void preMessagePump()
		{
			_shiftDown = GetKeyState(VK_SHIFT) & 0x8000;
			_ctrlDown = GetKeyState(VK_CONTROL) & 0x8000;
			_altDown = GetKeyState(VK_MENU) & 0x8000;
		}
	private:
		static LRESULT handleMsg(HWND nWnd, UINT smg, WPARAM wParam, LPARAM lParam);
		LRESULT handleMsgImpl(HWND nWnd, UINT smg, WPARAM wParam, LPARAM lParam);
		bool ready() const noexcept
		{
			return !!_dispatcher;
		}
	private:
		static constexpr  const wchar_t* _wndCLassName = L"CSyrenWindowClass";
		HINSTANCE _hInst;
		int _width;
		int _height;
		const wchar_t* _title;
		HWND _hWnd;
		input::InputDispatcher* _dispatcher;
		bool _shiftDown = false;
		bool _ctrlDown = false;
		bool _altDown = false;
	};
}



#endif
