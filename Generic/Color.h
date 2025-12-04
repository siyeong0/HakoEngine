#pragma once
#include <cmath>
#include <algorithm>
#include "Math/FVector4.h"

struct Color;

struct RGBA
{
	uint8_t	r;
	uint8_t	g;
	uint8_t	b;
	uint8_t	a;

	uint8_t& operator[](size_t i) { return *(&r + i); }
	const uint8_t& operator[](size_t i) const { return *(&r + i); }

	operator Color() const noexcept;

	static constexpr RGBA Black()		noexcept { return RGBA{ 0,   0,   0,   255 }; }
	static constexpr RGBA White()		noexcept { return RGBA{ 255, 255, 255, 255 }; }
	static constexpr RGBA Red()			noexcept { return RGBA{ 255, 0,   0,   255 }; }
	static constexpr RGBA Blue()		noexcept { return RGBA{ 0,   0,   255, 255 }; }
	static constexpr RGBA Green()		noexcept { return RGBA{ 0,   255, 0,   255 }; }
	static constexpr RGBA Cyan()		noexcept { return RGBA{ 0,   255, 255, 255 }; }
	static constexpr RGBA Magenta()		noexcept { return RGBA{ 255, 0,   255, 255 }; }
	static constexpr RGBA Yellow()		noexcept { return RGBA{ 255, 255, 0,   255 }; }
	static constexpr RGBA Orange()		noexcept { return RGBA{ 255, 165, 0,   255 }; }
	static constexpr RGBA Purple()		noexcept { return RGBA{ 128, 0,   128, 255 }; }
	static constexpr RGBA Teal()		noexcept { return RGBA{ 0,   128, 128, 255 }; }
	static constexpr RGBA Navy()		noexcept { return RGBA{ 0,   0,   128, 255 }; }
	static constexpr RGBA Gray()		noexcept { return RGBA{ 128, 128, 128, 255 }; }
	static constexpr RGBA LightGray()	noexcept { return RGBA{ 192, 192, 192, 255 }; }
	static constexpr RGBA DarkGray()	noexcept { return RGBA{ 64,  64,  64,  255 }; }
	static constexpr RGBA Pink()		noexcept { return RGBA{ 255, 192, 203, 255 }; }
	static constexpr RGBA Transparent()	noexcept { return RGBA{ 0,   0,   0,   0 }; }
};

struct Color
{
	float r;
	float g;
	float b;
	float a;

	float& operator[](std::size_t i) { return *(&r + i); }
	const float& operator[](std::size_t i) const { return *(&r + i); }

	operator RGBA() const noexcept;
	operator FVector4() const noexcept { return FVector4{ r, g, b, a }; }

	constexpr Color() : r(0.f), g(0.f), b(0.f), a(1.f) {}
	constexpr Color(float r, float g, float b, float a = 1.f) : r(r), g(g), b(b), a(a) {}
	constexpr Color(const FVector4& v) : r(v.x), g(v.y), b(v.z), a(v.w) {}

	static constexpr Color Black()      noexcept { return Color{ 0.0f, 0.0f, 0.0f, 1.0f }; }
	static constexpr Color White()      noexcept { return Color{ 1.0f, 1.0f, 1.0f, 1.0f }; }
	static constexpr Color Red()        noexcept { return Color{ 1.0f, 0.0f, 0.0f, 1.0f }; }
	static constexpr Color Blue()       noexcept { return Color{ 0.0f, 0.0f, 1.0f, 1.0f }; }
	static constexpr Color Green()      noexcept { return Color{ 0.0f, 1.0f, 0.0f, 1.0f }; }
	static constexpr Color Cyan()       noexcept { return Color{ 0.0f, 1.0f, 1.0f, 1.0f }; }
	static constexpr Color Magenta()    noexcept { return Color{ 1.0f, 0.0f, 1.0f, 1.0f }; }
	static constexpr Color Yellow()     noexcept { return Color{ 1.0f, 1.0f, 0.0f, 1.0f }; }
	static constexpr Color Orange()     noexcept { return Color{ 1.0f, 165.f / 255.f, 0.0f, 1.0f }; }
	static constexpr Color Purple()     noexcept { return Color{ 128.f / 255.f, 0.0f, 128.f / 255.f, 1.0f }; }
	static constexpr Color Teal()       noexcept { return Color{ 0.0f, 128.f / 255.f, 128.f / 255.f, 1.0f }; }
	static constexpr Color Navy()       noexcept { return Color{ 0.0f, 0.0f, 128.f / 255.f, 1.0f }; }
	static constexpr Color Gray()       noexcept { return Color{ 128.f / 255.f, 128.f / 255.f, 128.f / 255.f, 1.0f }; }
	static constexpr Color LightGray()  noexcept { return Color{ 192.f / 255.f, 192.f / 255.f, 192.f / 255.f, 1.0f }; }
	static constexpr Color DarkGray()   noexcept { return Color{ 64.f / 255.f,  64.f / 255.f,  64.f / 255.f,  1.0f }; }
	static constexpr Color Pink()       noexcept { return Color{ 1.0f, 192.f / 255.f, 203.f / 255.f, 1.0f }; }
	static constexpr Color Transparent() noexcept { return Color{ 0.0f, 0.0f, 0.0f, 0.0f }; }

	float Dot(const Color& o) const { return r * o.r + g * o.g + b * o.b + a * o.a; }
	float Magnitude() const { return std::sqrt(Dot(*this)); }
	float SqrMagnitude() const { return Dot(*this); }
	float Length() const { return Magnitude(); }

	Color Normalized() const { return *this / Length(); }
	void Normalize() { *this = Normalized(); }

	Color& operator+=(const Color& o) { r += o.r; g += o.g; b += o.b; a += o.a; return *this; }
	Color& operator-=(const Color& o) { r -= o.r; g -= o.g; b -= o.b; a -= o.a; return *this; }
	Color& operator*=(const Color& o) { r *= o.r; g *= o.g; b *= o.b; a *= o.a; return *this; }
	Color& operator/=(const Color& o) { r /= o.r; g /= o.g; b /= o.b; a /= o.a; return *this; }
	Color& operator*=(float s) { r *= s; g *= s; b *= s; a *= s; return *this; }
	Color& operator/=(float s) { float inv = 1.0f / s; r *= inv; g *= inv; b *= inv; a *= inv; return *this; }

	static float Dot(const Color& a, const Color& b) { return a.Dot(b); }

	static float Magnitude(const Color& c) { return c.Magnitude(); }
	static float SqrMagnitude(const Color& c) { return c.SqrMagnitude(); }
	static float Length(const Color& c) { return c.Length(); }

	static Color Normalize(const Color& c) { return c.Normalized(); }

	static Color Abs(const Color& c)
	{
		return Color{
			std::fabs(c.r),
			std::fabs(c.g),
			std::fabs(c.b),
			std::fabs(c.a)
		};
	}

	static Color Min(const Color& a, const Color& b)
	{
		return Color{
			std::fmin(a.r, b.r),
			std::fmin(a.g, b.g),
			std::fmin(a.b, b.b),
			std::fmin(a.a, b.a)
		};
	}

	static Color Max(const Color& a, const Color& b)
	{
		return Color{
			std::fmax(a.r, b.r),
			std::fmax(a.g, b.g),
			std::fmax(a.b, b.b),
			std::fmax(a.a, b.a)
		};
	}

	static float MinComponent(const Color& c)
	{
		return std::fmin(std::fmin(c.r, c.g), std::fmin(c.b, c.a));
	}

	static float MaxComponent(const Color& c)
	{
		return std::fmax(std::fmax(c.r, c.g), std::fmax(c.b, c.a));
	}

	static Color Clamp(const Color& value, float mn, float mx)
	{
		return Clamp(value, Color{ mn, mn, mn, mn }, Color{ mx, mx, mx, mx });
	}

	static Color Clamp(const Color& value, const Color& mn, const Color& mx)
	{
		return Color{
			std::fmax(mn.r, std::fmin(value.r, mx.r)),
			std::fmax(mn.g, std::fmin(value.g, mx.g)),
			std::fmax(mn.b, std::fmin(value.b, mx.b)),
			std::fmax(mn.a, std::fmin(value.a, mx.a))
		};
	}

	static Color Lerp(const Color& a, const Color& b, float t)
	{
		return Color{
			a.r + (b.r - a.r) * t,
			a.g + (b.g - a.g) * t,
			a.b + (b.b - a.b) * t,
			a.a + (b.a - a.a) * t
		};
	}

	static Color SmoothStep(const Color& a, const Color& b, float t)
	{
		t = std::fmax(0.f, std::fmin(t, 1.f));
		float s = t * t * (3.f - 2.f * t);
		return Color{
			a.r + (b.r - a.r) * s,
			a.g + (b.g - a.g) * s,
			a.b + (b.b - a.b) * s,
			a.a + (b.a - a.a) * s
		};
	}

	Color operator-() const { return Color{ -r, -g, -b, -a }; }

	Color operator+(const Color& o) const { return Color{ r + o.r, g + o.g, b + o.b, a + o.a }; }
	Color operator-(const Color& o) const { return Color{ r - o.r, g - o.g, b - o.b, a - o.a }; }
	Color operator*(const Color& o) const { return Color{ r * o.r, g * o.g, b * o.b, a * o.a }; }
	Color operator/(const Color& o) const { return Color{ r / o.r, g / o.g, b / o.b, a / o.a }; }

	Color operator+(float s) const { return Color{ r + s, g + s, b + s, a + s }; }
	Color operator-(float s) const { return Color{ r - s, g - s, b - s, a - s }; }
	Color operator*(float s) const { return Color{ r * s, g * s, b * s, a * s }; }
	Color operator/(float s) const { float inv = 1.0f / s; return Color{ r * inv, g * inv, b * inv, a * inv }; }

	bool operator==(const Color& o) const { return r == o.r && g == o.g && b == o.b && a == o.a; }
	bool operator!=(const Color& o) const { return !(*this == o); }

	friend Color operator*(float s, const Color& c) { return Color{ c.r * s, c.g * s, c.b * s, c.a * s }; }
};


inline RGBA::operator Color() const noexcept
{
	constexpr float inv255 = 1.0f / 255.0f;
	return Color{ r * inv255, g * inv255, b * inv255, a * inv255 };
}

inline Color::operator RGBA() const noexcept
{
	auto to8 = [](float x) -> std::uint8_t {return static_cast<std::uint8_t>(std::lround(std::clamp(x, 0.0f, 1.0f) * 255.0f)); };
	return RGBA{ to8(r), to8(g), to8(b), to8(a) };
}

inline void HSVtoRGB(float h, float s, float v, float& r, float& g, float& b)
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