#pragma once
#include "Common/Common.h"
#include "IVector3.h"

struct IBounds
{
	IVector3 Min;
	IVector3 Max;

	constexpr IBounds() : Min(IVector3::MaxValue()), Max(IVector3::MinValue()) {}
	constexpr IBounds(const IVector3& min, const IVector3& max) : Min(min), Max(max) {}

	inline IVector3 Center() const { return (Min + Max) * 0.5f; }
	inline IVector3 Size() const { return Max - Min; }
	inline IVector3 Extents() const { return Size() * 0.5f; }

	inline int64_t Volume() const
	{
		const IVector3 s = Size();
		return static_cast<int64_t>(s.x) * static_cast<int64_t>(s.y) * static_cast<int64_t>(s.z);
	}

	inline void Encapsulate(const IVector3& p) 
	{
		Min = IVector3::Min(Min, p);
		Max = IVector3::Max(Max, p);
	}
	inline void Encapsulate(const IBounds& o)
	{
		Encapsulate(o.Min);
		Encapsulate(o.Max);
	}

	inline bool Contains(const IVector3& p) const 
	{
		return (p.x >= Min.x && p.x <= Max.x) &&
			(p.y >= Min.y && p.y <= Max.y) &&
			(p.z >= Min.z && p.z <= Max.z);
	}

	inline bool Overlaps(const IBounds& o) const 
	{
		return (Min.x <= o.Max.x && Max.x >= o.Min.x) &&
			(Min.y <= o.Max.y && Max.y >= o.Min.y) &&
			(Min.z <= o.Max.z && Max.z >= o.Min.z);
	}

	static inline bool Overlaps(const IBounds& a, const IBounds& b)
	{
		return a.Overlaps(b);
	}
};

static_assert(sizeof(IBounds) == 24, "Wrong size of IBounds struct");
