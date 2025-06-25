#include "pch.h"
#include "application.h"
#include "renderer.h"
#include "input_dispatcher.h"
#include <cassert>
#include <format>


wchar_t* translateErrorCode(HRESULT hr) noexcept
{
	wchar_t* pMsgBuf = nullptr;
	DWORD nMsgLen = FormatMessage(
		FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
		nullptr, hr, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
		reinterpret_cast<LPWSTR>(&pMsgBuf), 0, nullptr);

	if (nMsgLen == 0)
		return nullptr;
	return pMsgBuf;
}


namespace csyren::core
{
	Application::Application() :
		_window(1200, 786, L"csyren engine")
	{ }

	Application::~Application()
	{
	}


	bool Application::init()
	{
		auto hWnd = _window.init();
		if (!hWnd)
		{
			auto error = translateErrorCode(GetLastError());
			auto error_str = std::format(TEXT("Failed init main window:{}"), error ? error : TEXT("undefined error code"));
			MessageBox(0,error_str.c_str(), 0, 0);
			return false;
		}
		_window.setInputDispatcher(&_inputDispatcher);
		
		return true;
	}


	int Application::run()
	{
		_window.show();
		MSG msg = { 0 };
		while (msg.message != WM_QUIT)
		{
			_window.preMessagePump();
			if (PeekMessage(&msg, 0, 0, 0, PM_REMOVE))
			{
				TranslateMessage(&msg);
				DispatchMessage(&msg);
			}
			_inputDispatcher.update();
			_scene.update(_time);
			_scene.draw(_render);
		}
		return static_cast<int>(msg.wParam);
	}

	
}
