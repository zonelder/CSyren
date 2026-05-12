#pragma once
#include "core/window.h"
#include "log.h"

#if defined(_DEBUG) || defined(DEBUG)
#define CS_ENABLE_ASSERTS 1
#else
#define CS_ENABLE_ASSERTS 0
#endif

#if defined(_MSC_VER)
#define CS_DEBUG_BREAK() __debugbreak()
#elif defined(__clang__) || defined(__GNUC__)
#define CS_DEBUG_BREAK() __builtin_trap()
#else
#include <cstdlib>
#define CS_DEBUG_BREAK() std::abort()
#endif

namespace csyren::cstdmf::details
{
    struct AssertInfo
    {
        const char* expression;
        const char* message;
        const char* file;
        int         line;
    };

    // пользователь может переопределить
    inline void handleAssert(const AssertInfo& info)
    {
        log::error("ASSERT FAILED\n"
            "Expr: {}\n"
            "Msg : {}\n"
            "File: {}:{}\n",
            info.expression,
            info.message ? info.message : "<none>",
            info.file,
            info.line);
    }

    [[noreturn]] inline void handleFatal(const AssertInfo& e)
    {
        auto msg = std::format("Fatal error:\n{}\n\nLocation:\n{} : {}\n", e.message, e.file, e.line);
        log::error(msg);
#if CS_ENABLE_ASSERTS
        OutputDebugStringA(msg.c_str());
        MessageBoxA(nullptr, msg.c_str(), "Fatal Error", MB_ICONERROR);
        CS_DEBUG_BREAK();
#else
        MessageBoxA(nullptr, msg.c_str(), "Fatal Error", MB_ICONERROR);
#endif
        std::exit(EXIT_FAILURE);
    }
}

#define CS_FATAL(code, msg) \
    csyren::cstdmf::details::handleFatal({ code, msg, __FILE__, __LINE__ })

#if CS_ENABLE_ASSERTS
#define CS_ASSERT(expr) \
        do { \
            if (!(expr)) { \
                csyren::cstdmf::details::handleAssert({ #expr, nullptr, __FILE__, __LINE__ }); \
                CS_DEBUG_BREAK(); \
            } \
        } while (0)

#define CS_ASSERT_MSG(expr, msg) \
        do { \
            if (!(expr)) { \
                csyren::cstdmf::details::handleAssert({ #expr, msg, __FILE__, __LINE__ }); \
                CS_DEBUG_BREAK(); \
            } \
        } while (0)

#else
    #define BW_ASSERT(expr)        ((void)0)
    #define BW_ASSERT_MSG(expr, m) ((void)0)
#endif