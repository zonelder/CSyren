#define CONSOLE_ENABLE

#include <iostream>
#include "application.h"
#include "startup_options.h"
#include <shellapi.h>
#include "cstdmf/log.h"
#pragma comment(lib, "shell32.lib")


int CALLBACK WinMain(HINSTANCE hInstance, HINSTANCE pPrevInstance, LPSTR plCmdLine, INT nCmdShow)
{

#ifdef CONSOLE_ENABLE
	FILE* conout = stdout;
	AllocConsole();
	AttachConsole(GetCurrentProcessId());
	freopen_s(&conout, "CON", "w", stdout);
	std::cout.sync_with_stdio(true);
#endif

    std::filesystem::path logPath = "csyren.log";
    auto showFailure = [&](const std::string& reason) {
        const std::string message = reason + "\n\nLog: " + csyren::core::pathToUtf8(logPath);
        try { MessageBoxW(nullptr, csyren::core::pathFromUtf8(message).c_str(), L"CSyren startup failed", MB_OK | MB_ICONERROR); }
        catch (...) { MessageBoxA(nullptr, message.c_str(), "CSyren startup failed", MB_OK | MB_ICONERROR); }
    };
    struct LogLifetime { ~LogLifetime() { csyren::log::shutdown(); } } logLifetime;
    try
    {
        // Open the log before parsing arguments or loading any project settings.
        logPath = std::filesystem::absolute(logPath);
        if (!csyren::log::init(logPath))
        {
            const auto failedPath = logPath;
            logPath = std::filesystem::temp_directory_path() / "csyren.log";
            if (!csyren::log::init(logPath)) throw std::runtime_error("Cannot open startup log");
            csyren::log::warning("Cannot open log '{}'; using '{}'", csyren::core::pathToUtf8(failedPath), csyren::core::pathToUtf8(logPath));
        }
        csyren::log::info("CSyren process startup; command line='{}', working directory='{}', log='{}'",
            csyren::core::pathToUtf8(GetCommandLineW()), csyren::core::pathToUtf8(std::filesystem::current_path()),
            csyren::core::pathToUtf8(logPath));
        int count = 0;
        auto* arguments = CommandLineToArgvW(GetCommandLineW(), &count);
        if (!arguments) throw std::runtime_error("CommandLineToArgvW failed: " + std::to_string(GetLastError()));
        struct ArgumentsLifetime { LPWSTR* value; ~ArgumentsLifetime() { LocalFree(value); } } argumentsLifetime{ arguments };
        std::vector<std::wstring_view> values;
        for (int i = 1; i < count; ++i) values.emplace_back(arguments[i]);
        const auto options = csyren::StartupOptions::parse(values);
        csyren::Application app;
        if (!app.init(options.project)) throw std::runtime_error("Application::init returned false");
        return app.run();
    }
    catch (const std::exception& error)
    {
        csyren::log::exception(error);
        showFailure(error.what());
        return -1;
    }
    catch (...)
    {
        const std::string message = "Non-standard application exception";
        csyren::log::error(message);
        showFailure(message);
        return -1;
    }
}
