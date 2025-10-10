#pragma once
#include "Common/Common.h"
#include <cmath>
#include <numeric>
#include <limits>

#undef max

using FLOAT = float;

struct FVector2
{
    FLOAT x;
    FLOAT y;

    FVector2() = default;
    ~FVector2() = default;

    FVector2(FLOAT x, FLOAT y) : x(x), y(y) {}
    FVector2(const FVector2& other) : x(other.x), y(other.y) {}
    FVector2& operator=(const FVector2& other) { x = other.x; y = other.y; return *this; }
    inline FLOAT& operator[](size_t idx) { return (&x)[idx]; }
    inline const FLOAT& operator[](size_t idx) const { return (&x)[idx]; }

    static inline FVector2 Zero() { return FVector2{ 0.f, 0.f }; }
    static inline FVector2 One() { return FVector2{ 1.f, 1.f }; }
    static inline FVector2 UnitX() { return FVector2{ 1.f, 0.f }; }
    static inline FVector2 UnitY() { return FVector2{ 0.f, 1.f }; }
    static inline FVector2 FMaxValue() { constexpr FLOAT v = std::numeric_limits<FLOAT>::max();  return FVector2{ v, v }; }
    static inline FVector2 FMinValue() { constexpr FLOAT v = std::numeric_limits<FLOAT>::lowest();  return FVector2{ v, v }; }
    static inline FVector2 Up() { return FVector2{ 0.f, 1.f }; }
    static inline FVector2 Down() { return FVector2{ 0.f, -1.f }; }
    static inline FVector2 Right() { return FVector2{ 1.f, 0.f }; }
    static inline FVector2 Left() { return FVector2{ -1.f, 0.f }; }

    inline FLOAT Dot(const FVector2& other) const { return x * other.x + y * other.y; }

    inline FLOAT Magnitude() const { return std::sqrt(SqrMagnitude()); }
    inline FLOAT SqrMagnitude() const { return Dot(*this); };
    inline FLOAT Length() const { return Magnitude(); }

    inline FVector2 Normalized() const { return *this / Length(); }
    inline void Normalize() { *this = Normalized(); }

    static inline float Dot(const FVector2& a, const FVector2& b) { return a.Dot(b); }

    static inline FLOAT Magnitude(const FVector2& vec) { return vec.Magnitude(); }
    static inline FLOAT SqrMagnitude(const FVector2& vec) { return vec.SqrMagnitude(); }
    static inline FLOAT Length(const FVector2& vec) { return vec.Length(); }

    static inline FVector2 Normalize(const FVector2& vec) { return vec.Normalized(); }

    static inline FVector2 Abs(const FVector2& vec) { return FVector2{ std::fabs(vec.x), std::fabs(vec.y) }; }
    static inline FVector2 Min(const FVector2& a, const FVector2& b) { return FVector2{ std::fmin(a.x, b.x), std::fmin(a.y, b.y) }; }
    static inline FVector2 Max(const FVector2& a, const FVector2& b) { return FVector2{ std::fmax(a.x, b.x), std::fmax(a.y, b.y) }; }

    static inline FVector2 Clamp(const FVector2& value, const FVector2& min, const FVector2& max)
    {
        return FVector2{
            std::fmax(min.x, std::fmin(value.x, max.x)),
            std::fmax(min.y, std::fmin(value.y, max.y))
        };
    }
    static inline FVector2 Lerp(const FVector2& a, const FVector2& b, FLOAT t)
    {
        return FVector2{
            a.x + (b.x - a.x) * t,
            a.y + (b.y - a.y) * t
        };
    }
    static inline FVector2 SmoothStep(const FVector2& a, const FVector2& b, FLOAT t)
    {
        t = std::fmax(0.f, std::fmin(t, 1.f)); // keep clamped version
        const FLOAT s = t * t * (3.f - 2.f * t);
        return FVector2{
            a.x + (b.x - a.x) * s,
            a.y + (b.y - a.y) * s
        };
    }

    inline FVector2 operator-() const { return FVector2{ -x, -y }; }

    inline FVector2 operator+(const FVector2& rhs) const { return FVector2{ x + rhs.x, y + rhs.y }; }
    inline FVector2 operator-(const FVector2& rhs) const { return FVector2{ x - rhs.x, y - rhs.y }; }
    inline FVector2 operator*(const FVector2& rhs) const { return FVector2{ x * rhs.x, y * rhs.y }; }
    inline FVector2 operator*(FLOAT s) const { return FVector2{ x * s, y * s }; }
    inline FVector2 operator/(FLOAT s) const { return FVector2{ x / s, y / s }; }

    inline bool operator==(const FVector2& rhs) const { return (x == rhs.x) && (y == rhs.y); }
    inline bool operator!=(const FVector2& rhs) const { return !(*this == rhs); }

    inline FVector2& operator+=(const FVector2& rhs) { x += rhs.x; y += rhs.y; return *this; }
    inline FVector2& operator-=(const FVector2& rhs) { x -= rhs.x; y -= rhs.y; return *this; }
    inline FVector2& operator*=(const FVector2& rhs) { x *= rhs.x; y *= rhs.y; return *this; }
    inline FVector2& operator*=(FLOAT s) { x *= s; y *= s; return *this; }
    inline FVector2& operator/=(FLOAT s) { x /= s; y /= s; return *this; }

    friend inline FVector2 operator*(FLOAT s, const FVector2& v) { return FVector2{ s * v.x, s * v.y }; }
};
static_assert(sizeof(FVector2) == 8, "FVector2 size mismatch");