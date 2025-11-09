#pragma once
#include "Common/Common.h"
#include "FVector3.h"

struct Bounds
{
	FVector3 Min;
	FVector3 Max;

	constexpr Bounds() : Min(FVector3::FMaxValue()), Max(FVector3::FMinValue()) {};
	constexpr Bounds(const FVector3& min, const FVector3& max) : Min(min), Max(max) {}

	inline FVector3 Center() const { return (Min + Max) * 0.5f; }
	inline FVector3 Size() const { return Max - Min; }
	inline FVector3 Extents() const { return Size() * 0.5f; }
	inline float Volume() const { FVector3 size = Size(); return size.x * size.y * size.z; }

	inline void Encapsulate(const FVector3& point)
	{
		Min = FVector3::Min(Min, point);
		Max = FVector3::Max(Max, point);
	}

	inline void Encapsulate(const Bounds& other)
	{
		Encapsulate(other.Min);
		Encapsulate(other.Max);
	}

	inline bool Contains(const FVector3& point) const
	{
		return (point.x >= Min.x && point.x <= Max.x) &&
			(point.y >= Min.y && point.y <= Max.y) &&
			(point.z >= Min.z && point.z <= Max.z);
	}

	inline bool Overlaps(const Bounds& other) const
	{
		return (Min.x <= other.Max.x && Max.x >= other.Min.x) &&
			(Min.y <= other.Max.y && Max.y >= other.Min.y) &&
			(Min.z <= other.Max.z && Max.z >= other.Min.z);
	}

	static bool Overlaps(const Bounds& a, const Bounds& b)
	{
		return a.Overlaps(b);
	}
};
static_assert(sizeof(Bounds) == 24, "Wrong size of Bounds struct");