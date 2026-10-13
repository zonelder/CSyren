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

    [[noreturn]] inline void failAssert(const AssertInfo& info)
    {
        log::error(
            "ASSERT FAILED\n"
            "Expr: {}\n"
            "Msg : {}\n"
            "File: {}:{}\n",
            info.expression,
            info.message ? info.message : "<none>",
            info.file,
            info.line);

#if CS_ENABLE_ASSERTS
        CS_DEBUG_BREAK();
#endif

        std::abort();
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

//@brief failuer means that we can not continue execution, application must be terminated,most times it should
// be used in case of unrecoverable errors, like memory allocation failure, critical resource missing, etc
#define CS_ASSERT(expr) \
        do { \
            if (!(expr)) { \
                csyren::cstdmf::details::failAssert({ #expr, nullptr, __FILE__, __LINE__ }); \
            } \
        } while (0)

//@brief failuer means that we can not continue execution, application must be terminated,most times it should
// be used in case of unrecoverable errors, like memory allocation failure, critical resource missing, etc
#define CS_ASSERT_MSG(expr, msg) \
        do { \
            if (!(expr)) { \
                csyren::cstdmf::details::failAssert({ #expr, msg, __FILE__, __LINE__ }); \
            } \
        } while (0)


#if CS_ENABLE_ASSERTS

//@brief failer means that some contract is broken, 
// this check is only active in debug builds, and erased in release builds, so it should be used for checking preconditions, postconditions, invariants, etc
#define CS_DEBUG_ASSERT(expr) \
    CS_ASSERT(expr)

//@brief failer means that some contract is broken, 
// this check is only active in debug builds, and erased in release builds, so it should be used for checking preconditions, postconditions, invariants, etc
#define CS_DEBUG_ASSERT_MSG(expr, msg) \
    CS_ASSERT_MSG(expr, msg)

#else

//@brief failer means that some contract is broken, 
// this check is only active in debug builds, and erased in release builds, so it should be used for checking preconditions, postconditions, invariants, etc
#define CS_DEBUG_ASSERT(expr) \
    ((void)0)

//@brief failer means that some contract is broken, 
// this check is only active in debug builds, and erased in release builds, so it should be used for checking preconditions, postconditions, invariants, etc
#define CS_DEBUG_ASSERT_MSG(expr, msg) \
    ((void)0)

#endif
