#pragma once
#include <cstdint>

union RGBA
{
	struct
	{
		uint8_t	r;
		uint8_t	g;
		uint8_t	b;
		uint8_t	a;
	};
	uint8_t bColorFactor[4];
};