#ifndef __CSYREN_LOG__
#define __CSYREN_LOG__

#include <string>
#include <string_view>
#include <format>
#include <fstream>
#include <iostream>
#include <mutex>
#include <filesystem>
#include <exception>
#include <typeinfo>
#include "window_clean.h"


namespace csyren::log
{
    enum class Level { Debug, Info, Warning, Error };
	inline std::ofstream g_logFile;
	inline std::mutex g_mutex;
    inline std::filesystem::path g_logPath;

    inline const char* toString(Level level)
    {
        switch (level)
        {
        case Level::Debug:   return "DEBUG";
        case Level::Info:    return "INFO";
        case Level::Warning: return "WARNING";
        case Level::Error:   return "ERROR";
        }
        return "INFO";
    }

	inline bool init(const std::filesystem::path& file = "csyren.log")
	{
        std::scoped_lock lock(g_mutex);
        if (g_logFile.is_open() && g_logPath == file) return g_logFile.good();
        if (g_logFile.is_open()) g_logFile.close();
        g_logFile.clear();
		g_logFile.open(file, std::ios::app);
        g_logPath = file;
        return g_logFile.is_open();
	}
    inline void shutdown()
    {
        std::scoped_lock lock(g_mutex);
        if (g_logFile.is_open())
            g_logFile.close();
    }

    inline void write(std::string_view msg)
    {
        std::scoped_lock lock(g_mutex);
        std::cout << msg << std::endl;
        const std::string debuggerMessage = std::string(msg) + "\n";
        OutputDebugStringA(debuggerMessage.c_str());
        if (g_logFile.is_open())
            g_logFile << msg << std::endl;
    }


    inline void write(Level level, std::string_view msg)
    {
        write(std::format("[{}] {}", toString(level), msg));
    }

    namespace details
    {
        // Not named `log`: a function with the namespace's own name (next to the CRT ::log
        // from <cmath>) made IntelliSense resolve `log::` to a function.
        template<typename... Args>
        void writeFormatted(Level level, std::format_string<Args...> fmt, Args&&... args)
        {
            write(level, std::format(fmt, std::forward<Args>(args)...));
        }
    }


    template<typename... Args>
    void debug(std::format_string<Args...> fmt, Args&&... args)
    {
        details::writeFormatted(Level::Debug, fmt, std::forward<Args>(args)...);
    }

    template<typename... Args>
    void info(std::format_string<Args...> fmt, Args&&... args)
    {
        details::writeFormatted(Level::Info, fmt, std::forward<Args>(args)...);
    }

    template<typename... Args>
    void warning(std::format_string<Args...> fmt, Args&&... args)
    {
        details::writeFormatted(Level::Warning, fmt, std::forward<Args>(args)...);
    }

    template<typename... Args>
    void error(std::format_string<Args...> fmt, Args&&... args)
    {
        details::writeFormatted(Level::Error, fmt, std::forward<Args>(args)...);
    }

    inline void error(std::string_view msg)
    {
        write(Level::Error, msg);
    }

    inline void exception(std::string_view context, const std::exception& error, unsigned depth = 0)
    {
        if (context.empty()) log::error("Exception [{}]: {}", typeid(error).name(), error.what());
        else log::error("Exception in {} [{}]: {}", context, typeid(error).name(), error.what());
        if (depth >= 16) return;
        const auto* nested = dynamic_cast<const std::nested_exception*>(&error);
        if (!nested || !nested->nested_ptr()) return;
        try { std::rethrow_exception(nested->nested_ptr()); }
        catch (const std::exception& cause) { exception("nested cause", cause, depth + 1); }
        catch (...) { log::error("Nested cause is a non-standard exception"); }
    }

    inline void exception(const std::exception& error) { exception({}, error); }

}


#endif
