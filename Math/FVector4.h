#pragma once
#include "Common/Common.h"
#include <cmath>
#include <numeric>
#include <limits>

#undef max

using FLOAT = float;

struct FVector4
{
	FLOAT x;
	FLOAT y;
	FLOAT z;
	FLOAT w;

	constexpr FVector4() : x(0.f), y(0.f), z(0.f), w(0.f) {}
	~FVector4() = default;

	constexpr FVector4(FLOAT x, FLOAT y, FLOAT z, FLOAT w) : x(x), y(y), z(z), w(w) {}
	constexpr FVector4(const FVector4& o) : x(o.x), y(o.y), z(o.z), w(o.w) {}

	FVector4& operator=(const FVector4& o) { x = o.x; y = o.y; z = o.z; w = o.w; return *this; }

	inline FLOAT& operator[](size_t idx) { return (&x)[idx]; }
	inline const FLOAT& operator[](size_t idx) const { return (&x)[idx]; }

	static inline constexpr FVector4 Zero() { return FVector4{ 0.f,0.f,0.f,0.f }; }
	static inline constexpr FVector4 One() { return FVector4{ 1.f,1.f,1.f,1.f }; }
	static inline constexpr FVector4 UnitX() { return FVector4{ 1.f,0.f,0.f,0.f }; }
	static inline constexpr FVector4 UnitY() { return FVector4{ 0.f,1.f,0.f,0.f }; }
	static inline constexpr FVector4 UnitZ() { return FVector4{ 0.f,0.f,1.f,0.f }; }
	static inline constexpr FVector4 UnitW() { return FVector4{ 0.f,0.f,0.f,1.f }; }
	static inline constexpr FVector4 FMaxValue() { constexpr FLOAT v = std::numeric_limits<FLOAT>::max(); return FVector4{ v,v,v,v }; }
	static inline constexpr FVector4 FMinValue() { constexpr FLOAT v = std::numeric_limits<FLOAT>::lowest(); return FVector4{ v,v,v,v }; }

	inline FLOAT Dot(const FVector4& o) const { return x * o.x + y * o.y + z * o.z + w * o.w; }

	inline FLOAT Magnitude() const { return std::sqrt(SqrMagnitude()); }
	inline FLOAT SqrMagnitude() const { return Dot(*this); }
	inline FLOAT Length() const { return Magnitude(); }

	inline FVector4 Normalized() const { return *this / Length(); }
	inline void Normalize() { *this = Normalized(); }

	inline FVector4& operator+=(const FVector4& o) { *this = *this + o; return *this; }
	inline FVector4& operator-=(const FVector4& o) { *this = *this - o; return *this; }
	inline FVector4& operator*=(const FVector4& o) { *this = *this * o; return *this; }
	inline FVector4& operator/=(const FVector4& o) { *this = *this / o; return *this; }
	inline FVector4& operator*=(FLOAT s) { *this = *this * s; return *this; }
	inline FVector4& operator/=(FLOAT s) { *this = *this / s; return *this; }

	static inline FLOAT Dot(const FVector4& a, const FVector4& b) { return a.Dot(b); }

	static inline FLOAT Magnitude(const FVector4& v) { return v.Magnitude(); }
	static inline FLOAT SqrMagnitude(const FVector4& v) { return v.SqrMagnitude(); }
	static inline FLOAT Length(const FVector4& v) { return v.Length(); }

	static inline FVector4 Normalize(const FVector4& v) { return v.Normalized(); }

	static inline FVector4 Abs(const FVector4& v)
	{
		return FVector4{ std::fabs(v.x), std::fabs(v.y), std::fabs(v.z), std::fabs(v.w) };
	}

	static inline FVector4 Min(const FVector4& a, const FVector4& b)
	{
		return FVector4{
			std::fmin(a.x,b.x),
			std::fmin(a.y,b.y),
			std::fmin(a.z,b.z),
			std::fmin(a.w,b.w)
		};
	}

	static inline FVector4 Max(const FVector4& a, const FVector4& b)
	{
		return FVector4{
			std::fmax(a.x,b.x),
			std::fmax(a.y,b.y),
			std::fmax(a.z,b.z),
			std::fmax(a.w,b.w)
		};
	}

	static inline float MinComponent(const FVector4& v)
	{
		return std::fmin(std::fmin(v.x, v.y), std::fmin(v.z, v.w));
	}

	static inline float MaxComponent(const FVector4& v)
	{
		return std::fmax(std::fmax(v.x, v.y), std::fmax(v.z, v.w));
	}

	static inline FVector4 Clamp(const FVector4& value, FLOAT mn, FLOAT mx)
	{
		return Clamp(value, FVector4{ mn,mn,mn,mn }, FVector4{ mx,mx,mx,mx });
	}

	static inline FVector4 Clamp(const FVector4& v, const FVector4& mn, const FVector4& mx)
	{
		return FVector4{
			std::fmax(mn.x, std::fmin(v.x, mx.x)),
			std::fmax(mn.y, std::fmin(v.y, mx.y)),
			std::fmax(mn.z, std::fmin(v.z, mx.z)),
			std::fmax(mn.w, std::fmin(v.w, mx.w))
		};
	}

	static inline FVector4 Lerp(const FVector4& a, const FVector4& b, FLOAT t)
	{
		return FVector4{
			a.x + (b.x - a.x) * t,
			a.y + (b.y - a.y) * t,
			a.z + (b.z - a.z) * t,
			a.w + (b.w - a.w) * t
		};
	}

	static inline FVector4 SmoothStep(const FVector4& a, const FVector4& b, FLOAT t)
	{
		t = std::fmax(0.f, std::fmin(t, 1.f));
		FLOAT s = t * t * (3.f - 2.f * t);
		return FVector4{
			a.x + (b.x - a.x) * s,
			a.y + (b.y - a.y) * s,
			a.z + (b.z - a.z) * s,
			a.w + (b.w - a.w) * s
		};
	}

	inline FVector4 operator-() const { return FVector4{ -x,-y,-z,-w }; }

	inline FVector4 operator+(const FVector4& r) const { return FVector4{ x + r.x, y + r.y, z + r.z, w + r.w }; }
	inline FVector4 operator-(const FVector4& r) const { return FVector4{ x - r.x, y - r.y, z - r.z, w - r.w }; }
	inline FVector4 operator*(const FVector4& r) const { return FVector4{ x * r.x, y * r.y, z * r.z, w * r.w }; }
	inline FVector4 operator/(const FVector4& r) const { return FVector4{ x / r.x, y / r.y, z / r.z, w / r.w }; }

	inline FVector4 operator+(FLOAT s) const { return FVector4{ x + s, y + s, z + s, w + s }; }
	inline FVector4 operator-(FLOAT s) const { return FVector4{ x - s, y - s, z - s, w - s }; }
	inline FVector4 operator*(FLOAT s) const { return FVector4{ x * s, y * s, z * s, w * s }; }
	inline FVector4 operator/(FLOAT s) const { return FVector4{ x / s, y / s, z / s, w / s }; }

	inline bool operator==(const FVector4& o) const { return x == o.x && y == o.y && z == o.z && w == o.w; }
	inline bool operator!=(const FVector4& o) const { return !(*this == o); }

	friend inline FVector4 operator*(FLOAT s, const FVector4& v) { return FVector4{ s * v.x, s * v.y, s * v.z, s * v.w }; }
};

static_assert(sizeof(FVector4) == 16, "FVector4 size mismatch");
