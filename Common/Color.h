#pragma once
#include <cstdint>
#include <algorithm>

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

	static constexpr Color Black()		noexcept { return Color{ 0.0f, 0.0f, 0.0f, 1.0f }; }
	static constexpr Color White()		noexcept { return Color{ 1.0f, 1.0f, 1.0f, 1.0f }; }
	static constexpr Color Red()		noexcept { return Color{ 1.0f, 0.0f, 0.0f, 1.0f }; }
	static constexpr Color Blue()		noexcept { return Color{ 0.0f, 0.0f, 1.0f, 1.0f }; }
	static constexpr Color Green()		noexcept { return Color{ 0.0f, 1.0f, 0.0f, 1.0f }; }
	static constexpr Color Cyan()		noexcept { return Color{ 0.0f, 1.0f, 1.0f, 1.0f }; }
	static constexpr Color Magenta()	noexcept { return Color{ 1.0f, 0.0f, 1.0f, 1.0f }; }
	static constexpr Color Yellow()		noexcept { return Color{ 1.0f, 1.0f, 0.0f, 1.0f }; }
	static constexpr Color Orange()		noexcept { return Color{ 1.0f, 165.f / 255.f, 0.0f, 1.0f }; }
	static constexpr Color Purple()		noexcept { return Color{ 128.f / 255.f, 0.0f, 128.f / 255.f, 1.0f }; }
	static constexpr Color Teal()		noexcept { return Color{ 0.0f, 128.f / 255.f, 128.f / 255.f, 1.0f }; }
	static constexpr Color Navy()		noexcept { return Color{ 0.0f, 0.0f, 128.f / 255.f, 1.0f }; }
	static constexpr Color Gray()		noexcept { return Color{ 128.f / 255.f, 128.f / 255.f, 128.f / 255.f, 1.0f }; }
	static constexpr Color LightGray()	noexcept { return Color{ 192.f / 255.f, 192.f / 255.f, 192.f / 255.f, 1.0f }; }
	static constexpr Color DarkGray()	noexcept { return Color{ 64.f / 255.f,  64.f / 255.f,  64.f / 255.f,  1.0f }; }
	static constexpr Color Pink()		noexcept { return Color{ 1.0f, 192.f / 255.f, 203.f / 255.f, 1.0f }; }
	static constexpr Color Transparent() noexcept { return Color{ 0.0f, 0.0f, 0.0f, 0.0f }; }
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