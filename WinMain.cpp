#include <iostream>
#include "application.h"


#define CONSOLE_ENABLE

int CALLBACK WinMain(HINSTANCE hInstance, HINSTANCE pPrevInstance, LPSTR plCmdLine, INT nCmdShow)
{

#ifdef CONSOLE_ENABLE
	FILE* conout = stdout;
	AllocConsole();
	AttachConsole(GetCurrentProcessId());
	freopen_s(&conout, "CON", "w", stdout);
#endif

	csyren::Application app{};
	if (!app.init())
	{
		return -1;
	}
	return app.run();
}
