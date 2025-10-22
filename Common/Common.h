#pragma once
#include <cstdio>
#include <cstdlib>
#include <cstdint>
#include <Windows.h>
#include <DirectXMath.h>

// Interface conventions
#define ENGINECALL __stdcall

// Math
#include "Math/Math.h"
using FLOAT2 = FVector2;
using FLOAT3 = FVector3;
struct FLOAT4
{
	float x;
	float y;
	float z;
	float w;
};

static_assert(sizeof(FLOAT2) == 8, "FLOAT2 size mismatch");
static_assert(sizeof(FLOAT3) == 12, "FLOAT3 size mismatch");
static_assert(sizeof(FLOAT4) == 16, "FLOAT4 size mismatch");

// Common definitions
#define DEFULAT_LOCALE_NAME	L"ko-kr"
HRESULT typedef (__stdcall* CREATE_INSTANCE_FUNC)(void* ppv);

#include "ASSERT_MACRO.h"
#include "INTEGER_MACRO.h"
#include "SAFE_CLEAN_MACRO.h"
