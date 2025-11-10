#pragma once
#include "Common/Common.h"
#include <cmath>
#include <numeric>
#include <limits>
#include "IVector3.h"

#undef max

using FLOAT = float;

struct FVector3
{
	FLOAT x;
	FLOAT y;
	FLOAT z;

	constexpr FVector3() : x(0.0f), y(0.0f), z(0.0f) {}
	~FVector3() = default;

	constexpr FVector3(FLOAT x, FLOAT y, FLOAT z) : x(x), y(y), z(z) {}
	constexpr FVector3(const FVector3& other) : x(other.x), y(other.y), z(other.z) {}

	FVector3& operator=(const FVector3& other) { x = other.x; y = other.y; z = other.z; return *this; }
	inline FLOAT& operator[](size_t idx) { return (&x)[idx]; }
	inline const FLOAT& operator[](size_t idx) const { return (&x)[idx]; }

	static inline constexpr FVector3 Zero() { return FVector3{ 0.f, 0.f, 0.f }; }
	static inline constexpr FVector3 One() { return FVector3{ 1.f, 1.f, 1.f }; }
	static inline constexpr FVector3 UnitX() { return FVector3{ 1.f, 0.f, 0.f }; }
	static inline constexpr FVector3 UnitY() { return FVector3{ 0.f, 1.f, 0.f }; }
	static inline constexpr FVector3 UnitZ() { return FVector3{ 0.f, 0.f, 1.f }; }
	static inline constexpr FVector3 FMaxValue() { constexpr FLOAT v = std::numeric_limits<FLOAT>::max();  return FVector3{ v,v,v, }; }
	static inline constexpr FVector3 FMinValue() { constexpr FLOAT v = std::numeric_limits<FLOAT>::lowest();  return FVector3{ v,v,v, }; }
	static inline constexpr FVector3 Up() { return FVector3{ 0.f, 1.f, 0.f }; }
	static inline constexpr FVector3 Down() { return FVector3{ 0.f, -1.f, 0.f }; }
	static inline constexpr FVector3 Right() { return FVector3{ 1.f, 0.f, 0.f }; }
	static inline constexpr FVector3 Left() { return FVector3{ -1.f, 0.f, 0.f }; }
	static inline constexpr FVector3 Forward() { return FVector3{ 0.f, 0.f, 1.f }; }
	static inline constexpr FVector3 Backward() { return FVector3{ 0.f, 0.f, -1.f }; }

	inline FLOAT Dot(const FVector3& other) const { return x * other.x + y * other.y + z * other.z; }
	inline FVector3 Cross(const FVector3& other) const { return FVector3{ y * other.z - z * other.y,z * other.x - x * other.z,x * other.y - y * other.x }; }

	inline FLOAT Magnitude() const { return std::sqrt(SqrMagnitude()); }
	inline FLOAT SqrMagnitude() const { return Dot(*this); };
	inline FLOAT Length() const { return Magnitude(); }

	inline FVector3 Normalized() const { return *this / Length(); }
	inline void Normalize() { *this = Normalized(); }

	inline FVector3& operator+=(const FVector3& other) { *this = *this + other; return *this; }
	inline FVector3& operator-=(const FVector3& other) { *this = *this - other; return *this; }
	inline FVector3& operator*=(const FVector3& other) { *this = *this * other; return *this; }
	inline FVector3& operator/=(const FVector3& other) { *this = *this / other; return *this; }
	inline FVector3& operator*=(FLOAT v) { *this = *this * v; return *this; }
	inline FVector3& operator/=(FLOAT v) { *this = *this / v; return *this; }

	static inline float Dot(const FVector3& a, const FVector3& b) { return a.Dot(b); }
	static inline FVector3 Cross(const FVector3& a, const FVector3& b) { return a.Cross(b); }

	static inline FLOAT Magnitude(const FVector3& vec) { return vec.Magnitude(); }
	static inline FLOAT SqrMagnitude(const FVector3& vec) { return vec.SqrMagnitude(); }
	static inline FLOAT Length(const FVector3& vec) { return vec.Length(); }

	static inline FVector3 Normalize(const FVector3& vec) { return vec.Normalized(); }

	static inline FVector3 Abs(const FVector3& vec) { return FVector3{ std::fabs(vec.x), std::fabs(vec.y), std::fabs(vec.z) }; }
	static inline FVector3 Min(const FVector3& a, const FVector3& b) { return FVector3{ std::fmin(a.x, b.x), std::fmin(a.y, b.y), std::fmin(a.z, b.z) }; }
	static inline FVector3 Max(const FVector3& a, const FVector3& b) { return FVector3{ std::fmax(a.x, b.x), std::fmax(a.y, b.y), std::fmax(a.z, b.z) }; }
	static inline float MinComponent(const FVector3& v) { return std::fmin(std::fmin(v.x, v.y), v.z); }
	static inline float MaxComponent(const FVector3& v) { return std::fmax(std::fmax(v.x, v.y), v.z); }

	static inline FVector3 Clamp(const FVector3& value, FLOAT min, FLOAT max)
	{
		return Clamp(value, FVector3{ min, min, min }, FVector3{ max, max, max });
	}
	static inline FVector3 Clamp(const FVector3& value, const FVector3& min, const FVector3& max)
	{
		return FVector3{
			std::fmax(min.x, std::fmin(value.x, max.x)),
			std::fmax(min.y, std::fmin(value.y, max.y)),
			std::fmax(min.z, std::fmin(value.z, max.z))
		};
	}
	static inline FVector3 Lerp(const FVector3& a, const FVector3& b, FLOAT t)
	{
		return FVector3{
			a.x + (b.x - a.x) * t,
			a.y + (b.y - a.y) * t,
			a.z + (b.z - a.z) * t
		};
	}
	static inline FVector3 SmoothStep(const FVector3& a, const FVector3& b, FLOAT t)
	{
		t = std::fmax(0.f, std::fmin(t, 1.f)); // keep clamped version
		const FLOAT s = t * t * (3.f - 2.f * t);
		return FVector3{
			a.x + (b.x - a.x) * s,
			a.y + (b.y - a.y) * s,
			a.z + (b.z - a.z) * s
		};
	}

	inline FVector3 operator-() const { return FVector3{ -x, -y, -z }; }

	inline FVector3 operator+(const FVector3& rhs) const { return FVector3{ x + rhs.x, y + rhs.y, z + rhs.z }; }
	inline FVector3 operator-(const FVector3& rhs) const { return FVector3{ x - rhs.x, y - rhs.y, z - rhs.z }; }
	inline FVector3 operator*(const FVector3& rhs) const { return FVector3{ x * rhs.x, y * rhs.y, z * rhs.z }; }
	inline FVector3 operator/(const FVector3& rhs) const { return FVector3{ x / rhs.x, y / rhs.y, z / rhs.z }; }
	inline FVector3 operator+(FLOAT s) const { return FVector3{ x + s, y + s, z + s }; }
	inline FVector3 operator-(FLOAT s) const { return FVector3{ x - s, y - s, z - s }; }
	inline FVector3 operator*(FLOAT s) const { return FVector3{ x * s, y * s, z * s }; }
	inline FVector3 operator/(FLOAT s) const { return FVector3{ x / s, y / s, z / s }; }

	inline bool operator==(const FVector3& rhs) const { return (x == rhs.x) && (y == rhs.y) && (z == rhs.z); }
	inline bool operator!=(const FVector3& rhs) const { return !(*this == rhs); }

	friend inline FVector3 operator*(FLOAT s, const FVector3& v) { return FVector3{ s * v.x, s * v.y, s * v.z }; }
};
static_assert(sizeof(FVector3) == 12, "FVector3 size mismatch");