#define CONSOLE_ENABLE

#include <iostream>
#include "application.h"


int CALLBACK WinMain(HINSTANCE hInstance, HINSTANCE pPrevInstance, LPSTR plCmdLine, INT nCmdShow)
{

#ifdef CONSOLE_ENABLE
	FILE* conout = stdout;
	AllocConsole();
	AttachConsole(GetCurrentProcessId());
	freopen_s(&conout, "CON", "w", stdout);
	std::cout.sync_with_stdio(true);
#endif

	csyren::Application app{};
	if (!app.init())
	{
		return -1;
	}
	return app.run();
}
