#pragma once
#include <cstdio>
#include <cstdlib>
#include <Windows.h>

// Calling convention
#define ENGINECALL __stdcall

// Typedef
#include "Color.h"
#include "Vertex.h"

#define DEFULAT_LOCALE_NAME		L"ko-kr"
HRESULT typedef (__stdcall* CREATE_INSTANCE_FUNC)(void* ppv);

// Assert
#if defined(_WIN32)
#define COREASSERT_DEBUG_BREAK() __debugbreak()
#elif defined(__GNUC__) || defined(__clang__)
#define COREASSERT_DEBUG_BREAK() __builtin_trap()
#else
#define COREASSERT_DEBUG_BREAK() std::abort()
#endif

#ifndef NDEBUG
#define COREASSERT_IMPL(expr, msg)                                          \
    do {                                                                      \
      if (!(expr)) {                                                          \
        std::fprintf(stderr,                                                  \
          "[ASSERT] %s:%d in %s\n  expr: %s\n  msg : %s\n",                   \
          __FILE__, __LINE__, __func__, #expr, (msg));                        \
        std::fflush(stderr);                                                  \
        COREASSERT_DEBUG_BREAK();                                             \
      }                                                                       \
    } while (0)

#define COREASSERT_NO_MSG(expr)                                             \
    do {                                                                      \
      if (!(expr)) {                                                          \
        std::fprintf(stderr,                                                  \
          "[ASSERT] %s:%d in %s\n  expr: %s\n",                               \
          __FILE__, __LINE__, __func__, #expr);                               \
        std::fflush(stderr);                                                  \
        COREASSERT_DEBUG_BREAK();                                             \
      }                                                                       \
    } while (0)

#define COREASSERT_GET_MACRO(_1,_2,NAME,...) NAME
#define ASSERT(...) COREASSERT_GET_MACRO(__VA_ARGS__, COREASSERT_IMPL, COREASSERT_NO_MSG)(__VA_ARGS__)

#else
#define ASSERT(...) ((void)0)
#endif
