#include <Windows.h>
#include <iostream>
#include "core/window.h"

#define CONSOLE_ENABLE

using namespace csyren;

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



int CALLBACK WinMain(HINSTANCE hInstance, HINSTANCE pPrevInstance, LPSTR plCmdLine, INT nCmdShow)
{

#ifdef CONSOLE_ENABLE
	FILE* conout = stdout;
	AllocConsole();
	AttachConsole(GetCurrentProcessId());
	freopen_s(&conout, "CON", "w", stdout);
#endif
	core::Window window(1200, 786, L"CSyrenEngine");
	core::input::InputDispatcher dispatcher;
	auto hWnd = window.init();
	if (!hWnd)
	{
		auto error = translateErrorCode(GetLastError());
		MessageBox(0, error ? error : TEXT("undefined error code"), 0, 0);
		return -1;
	}

	window.setInputDispatcher(&dispatcher);
	ShowWindow(hWnd, SW_SHOW);

	auto& keyboard = dispatcher.devices().keyboard();

	dispatcher.events().subscribe(core::input::InputEvent::Type::KeyUp, [](const core::input::InputEvent& event) {

		std::cout << "up : " << char(event.code) << "\n"; }
	);

	dispatcher.events().subscribe(core::input::InputEvent::Type::MouseButtonDown, [](const core::input::InputEvent& event)
		{
			std::cout << "mouse button pressed:" << event.code << "\n";
		});
	dispatcher.events().subscribe(core::input::InputEvent::Type::MouseMove, [](const core::input::InputEvent& event)
		{
			std::cout << "mouse button move:" << event.data.mouse.x << " " << event.data.mouse.y << "\n";
		});
	
	MSG msg = { 0 };
	while (msg.message != WM_QUIT)
	{
		window.preMessagePump();
		if (PeekMessage(&msg, 0, 0, 0, PM_REMOVE))
		{
			TranslateMessage(&msg);
			DispatchMessage(&msg);
		}
		//process all events
		dispatcher.update();

		if (keyboard.isKeyDown('Q'))
		{
			std::cout << "q is pressed\n";
		}
		
	}


	return 0;
}
