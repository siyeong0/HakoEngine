#pragma once
#include <cstdint>
#include <limits>
#include <algorithm>
#include <cmath>

using INT32 = int32_t;

struct IVector3
{
	INT32 x;
	INT32 y;
	INT32 z;

	// ctors / dtor
	constexpr IVector3() : x(0), y(0), z(0) {}
	~IVector3() = default;

	constexpr IVector3(INT32 x_, INT32 y_, INT32 z_) : x(x_), y(y_), z(z_) {}
	constexpr IVector3(const IVector3& o) : x(o.x), y(o.y), z(o.z) {}

	// assign / index
	IVector3& operator=(const IVector3& o) { x = o.x; y = o.y; z = o.z; return *this; }
	inline INT32& operator[](size_t idx) { return (&x)[idx]; }
	inline const INT32& operator[](size_t idx) const { return (&x)[idx]; }

	// constants
	static inline constexpr IVector3 Zero() { return { 0, 0, 0 }; }
	static inline constexpr IVector3 One() { return { 1, 1, 1 }; }
	static inline constexpr IVector3 UnitX() { return { 1, 0, 0 }; }
	static inline constexpr IVector3 UnitY() { return { 0, 1, 0 }; }
	static inline constexpr IVector3 UnitZ() { return { 0, 0, 1 }; }
	static inline constexpr IVector3 MaxValue() { constexpr INT32 v = std::numeric_limits<INT32>::max();     return { v, v, v }; }
	static inline constexpr IVector3 MinValue() { constexpr INT32 v = std::numeric_limits<INT32>::lowest();  return { v, v, v }; }
	static inline constexpr IVector3 Up() { return { 0, 1, 0 }; }
	static inline constexpr IVector3 Down() { return { 0,-1, 0 }; }
	static inline constexpr IVector3 Right() { return { 1, 0, 0 }; }
	static inline constexpr IVector3 Left() { return { -1, 0, 0 }; }
	static inline constexpr IVector3 Forward() { return { 0, 0, 1 }; }
	static inline constexpr IVector3 Backward() { return { 0, 0,-1 }; }

	// modifiers (component-wise)
	inline void operator+=(const IVector3& o) { x += o.x; y += o.y; z += o.z; }
	inline void operator-=(const IVector3& o) { x -= o.x; y -= o.y; z -= o.z; }
	inline void operator*=(INT32 s) { x *= s; y *= s; z *= s; }
	inline void operator/=(INT32 s) { x /= s; y /= s; z /= s; }
	inline void operator*=(float s)
	{
		x = static_cast<INT32>(std::lround(x * s));
		y = static_cast<INT32>(std::lround(y * s));
		z = static_cast<INT32>(std::lround(z * s));
	}
	inline void operator/=(float s)
	{
		x = static_cast<INT32>(std::lround(x / s));
		y = static_cast<INT32>(std::lround(y / s));
		z = static_cast<INT32>(std::lround(z / s));
	}

	// unary
	inline IVector3 operator-() const { return { -x, -y, -z }; }

	// arithmetic (component-wise and scalar)
	inline IVector3 operator+(const IVector3& r) const { return { x + r.x, y + r.y, z + r.z }; }
	inline IVector3 operator-(const IVector3& r) const { return { x - r.x, y - r.y, z - r.z }; }
	inline IVector3 operator*(const IVector3& r) const { return { x * r.x, y * r.y, z * r.z }; } // 주의: 정수 곱
	inline IVector3 operator*(INT32 s) const { return { x * s, y * s, z * s }; }
	inline IVector3 operator/(INT32 s) const { return { x / s, y / s, z / s }; }
	inline IVector3 operator*(float s) const
	{
		const float tx = std::lround(x * s);
		const float ty = std::lround(y * s);
		const float tz = std::lround(z * s);
		return { (INT32)tx, (INT32)ty, (INT32)tz };
	}
	inline IVector3 operator/(float s) const
	{
		const float tx = std::lround(x / s);
		const float ty = std::lround(y / s);
		const float tz = std::lround(z / s);
		return { (INT32)tx, (INT32)ty, (INT32)tz };
	}

	// compare
	inline bool operator==(const IVector3& r) const { return x == r.x && y == r.y && z == r.z; }
	inline bool operator!=(const IVector3& r) const { return !(*this == r); }

	// helpers
	static inline IVector3 Abs(const IVector3& v) { return { std::abs(v.x), std::abs(v.y), std::abs(v.z) }; }
	static inline IVector3 Min(const IVector3& a, const IVector3& b) { return { std::min(a.x,b.x), std::min(a.y,b.y), std::min(a.z,b.z) }; }
	static inline IVector3 Max(const IVector3& a, const IVector3& b) { return { std::max(a.x,b.x), std::max(a.y,b.y), std::max(a.z,b.z) }; }

	static inline IVector3 Clamp(const IVector3& v, INT32 mn, INT32 mx) {return Clamp(v, { mn,mn,mn }, { mx,mx,mx });}
	static inline IVector3 Clamp(const IVector3& v, const IVector3& mn, const IVector3& mx)
	{
		return {
			std::max(mn.x, std::min(v.x, mx.x)),
			std::max(mn.y, std::min(v.y, mx.y)),
			std::max(mn.z, std::min(v.z, mx.z))
		};
	}

	static inline IVector3 Lerp(const IVector3& a, const IVector3& b, float t) 
	{
		const float tx = std::lround(a.x + (b.x - a.x) * t);
		const float ty = std::lround(a.y + (b.y - a.y) * t);
		const float tz = std::lround(a.z + (b.z - a.z) * t);
		return { (INT32)tx, (INT32)ty, (INT32)tz };
	}

	friend inline IVector3 operator*(INT32 s, const IVector3& v) { return { s * v.x, s * v.y, s * v.z }; }
};

static_assert(sizeof(IVector3) == 12, "IVector3 size mismatch");
