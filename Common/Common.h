#pragma once
#include <cstdio>
#include <cstdlib>
#include <cstdint>
#include <Windows.h>

#include <DirectXMath.h>

// Calling convention
#define ENGINECALL __stdcall

// Math and Type definitions
constexpr float PI = 3.14159265358979323846f;
constexpr float TWO_PI = 6.2831853071795864769f;
constexpr float HALF_PI = 1.57079632679489661923f;
constexpr float INV_PI = 0.31830988618379067154f;

using uint = unsigned int;

struct int2
{
	int x;
	int y;
	
	int operator[](size_t idx) const { return (&x)[idx]; }
};

struct int3
{
	int x;
	int y;
	int z;

	int operator[](size_t idx) const { return (&x)[idx]; }
};

struct uint2
{
	uint x;
	uint y;

	uint operator[](size_t idx) const { return (&x)[idx]; }
};

struct uint3
{
	uint x;
	uint y;
	uint z;

	uint operator[](size_t idx) const { return (&x)[idx]; }
};

using Matrix4x4 = DirectX::XMMATRIX;

#include "FVector2.h"
using FLOAT2 = FVector2;
static_assert(sizeof(FLOAT2) == 8, "FLOAT2 size mismatch");

#include "FVector3.h"
using FLOAT3 = FVector3;
static_assert(sizeof(FLOAT3) == 12, "FLOAT3 size mismatch");

struct FLOAT4
{
	float x;
	float y;
	float z;
	float w;
};
static_assert(sizeof(FLOAT4) == 16, "FLOAT4 size mismatch");

#include "Bounds.h"
#include "Plane.h"

#include "Color.h"

static inline void HSVtoRGB(float h, float s, float v, float& r, float& g, float& b)
{
	h = std::fmodf(std::fmax(h, 0.f), 1.f) * 6.f;
	const int   i = (int)std::floor(h);
	const float f = h - i;
	const float p = v * (1.f - s);
	const float q = v * (1.f - s * f);
	const float t = v * (1.f - s * (1.f - f));
	switch (i) {
	default:
	case 0: r = v; g = t; b = p; break;
	case 1: r = q; g = v; b = p; break;
	case 2: r = p; g = v; b = t; break;
	case 3: r = p; g = q; b = v; break;
	case 4: r = t; g = p; b = v; break;
	case 5: r = v; g = p; b = q; break;
	}
}

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
