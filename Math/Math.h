#pragma once
constexpr float PI = 3.14159265358979323846f;
constexpr float TWO_PI = 6.2831853071795864769f;
constexpr float HALF_PI = 1.57079632679489661923f;
constexpr float INV_PI = 0.31830988618379067154f;

constexpr double DP_PI = 3.14159265358979323846264338327950288419716939937510;
constexpr double DP_TWO_PI = 6.28318530717958647692528676655900576839433879875020;
constexpr double DP_HALF_PI = 1.57079632679489661923132169163975144209858469968755;
constexpr double DP_INV_PI = 0.31830988618379067153776752674502872406891929148091;

constexpr float E = 2.71828182845904523536f;

constexpr float DegToRad(float degrees) { return degrees * (PI / 180.0f); }
constexpr float RadToDeg(float radians) { return radians * (180.0f / PI); }

#include "FVector2.h"
#include "FVector3.h"
#include "FVector4.h"
#include "Matrix.h"

#include "Bounds.h"
#include "Plane.h"

#include "IVector3.h"
#include "IBounds.h"

inline IVector3 ToIVector3(const FVector3& v)
{
	return IVector3(static_cast<INT32>(std::floor(v.x)),
		static_cast<INT32>(std::floor(v.y)),
		static_cast<INT32>(std::floor(v.z)));
}

inline FVector3 ToFVector3(const IVector3& v)
{
	return FVector3(static_cast<FLOAT>(v.x),
		static_cast<FLOAT>(v.y),
		static_cast<FLOAT>(v.z));
}