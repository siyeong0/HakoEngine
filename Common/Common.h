#pragma once
#include <cstdio>
#include <cstdlib>
#include <cstdint>
#include <Windows.h>

#include <DirectXMath.h>
using namespace DirectX;

// Calling convention
#define ENGINECALL __stdcall

// Typedef
using uint = unsigned int;
using Matrix4x4 = DirectX::XMMATRIX;

// Basic Structures
struct FLOAT2
{
    float x;
    float y;
};
static_assert(sizeof(FLOAT2) == 8, "FLOAT2 size mismatch");

struct FLOAT3
{
    float x;
    float y;
    float z;
};
static_assert(sizeof(FLOAT3) == 12, "FLOAT3 size mismatch");

struct FLOAT4
{
    float x;
    float y;
    float z;
    float w;
};
static_assert(sizeof(FLOAT4) == 16, "FLOAT4 size mismatch");

union RGBA
{
    struct
    {
        uint8_t	r;
        uint8_t	g;
        uint8_t	b;
        uint8_t	a;
    };
    uint8_t ColorData[4];
};

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
#define COREASSERT_IMPL(expr, msg)                                            \
    do {                                                                      \
      if (!(expr)) {                                                          \
        std::fprintf(stderr,                                                  \
          "[ASSERT] %s:%d in %s\n  expr: %s\n  msg : %s\n",                   \
          __FILE__, __LINE__, __func__, #expr, (msg));                        \
        std::fflush(stderr);                                                  \
        COREASSERT_DEBUG_BREAK();                                             \
      }                                                                       \
    } while (0)

#define COREASSERT_NO_MSG(expr)                                               \
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

// Safe Cleanup
#define SAFE_CLEANUP(ptr, deleter)                                           \
    do {                                                                     \
        auto& _p = (ptr);                                                    \
        if (_p) {                                                            \
            deleter(_p);                                                     \
            _p = nullptr;                                                    \
        }                                                                    \
    } while (0)

#define SAFE_RELEASE(p)         SAFE_CLEANUP(p, [](auto* x){ x->Release(); })
#define SAFE_FREE(p)            SAFE_CLEANUP(p, std::free)
#define SAFE_FREE_LIBRARY(h)    SAFE_CLEANUP(h, FreeLibrary)
#define SAFE_DELETE(p)          SAFE_CLEANUP(p, [](auto* x){ delete x; })
#define SAFE_DELETE_ARRAY(p)    SAFE_CLEANUP(p, [](auto* x){ delete[] x; })
#define SAFE_CLOSE_HANDLE(h)    SAFE_CLEANUP(h, [](auto x){ if (x != INVALID_HANDLE_VALUE) CloseHandle(x); })
