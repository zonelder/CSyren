#ifndef __CSYREN_LOG__
#define __CSYREN_LOG__

#include <string>
#include <format>
#include <fstream>
#include <iostream>
#include <mutex>

namespace csyren::log
{
    enum class Level { Debug, Info, Warning, Error };
	inline std::ofstream g_logFile;
	inline std::mutex g_mutex;

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

	inline void init(const std::string& file = "csyren.log")
	{
		g_logFile.open(file, std::ios::app);
	}
    inline void shutdown()
    {
        if (g_logFile.is_open())
            g_logFile.close();
    }

    inline void write(const std::string& msg)
    {
        std::scoped_lock lock(g_mutex);
        std::cout << msg << std::endl;
        if (g_logFile.is_open())
            g_logFile << msg << std::endl;
    }


    inline void write(Level level, const std::string& msg)
    {
        std::scoped_lock lock(g_mutex);
        std::string formatted = std::format("[{}] {}", toString(level), msg);
        write(formatted);
    }

    namespace
    {
        template<typename... Args>
        void log(Level level, std::format_string<Args...> fmt, Args&&... args)
        {
            write(level, std::format(fmt, std::forward<Args>(args)...));
        }
    }


    template<typename... Args>
    void debug(std::format_string<Args...> fmt, Args&&... args)
    {
        log(Level::Debug, fmt, std::forward<Args>(args)...);
    }

    template<typename... Args>
    void info(std::format_string<Args...> fmt, Args&&... args)
    {
        log(Level::Info, fmt, std::forward<Args>(args)...);
    }

    template<typename... Args>
    void warning(std::format_string<Args...> fmt, Args&&... args)
    {
        log(Level::Warning, fmt, std::forward<Args>(args)...);
    }

    template<typename... Args>
    void error(std::format_string<Args...> fmt, Args&&... args)
    {
        log(Level::Error, fmt, std::forward<Args>(args)...);
    }

}


#endif
