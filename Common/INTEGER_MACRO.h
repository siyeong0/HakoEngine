#pragma once

using uint = unsigned int;

struct int2
{
	int x;
	int y;

	int operator[](size_t idx) const { return (&x)[idx]; }
};

struct int3
{
	int x;
	int y;
	int z;

	int operator[](size_t idx) const { return (&x)[idx]; }
};

struct uint2
{
	uint x;
	uint y;

	uint operator[](size_t idx) const { return (&x)[idx]; }
};

struct uint3
{
	uint x;
	uint y;
	uint z;

	uint operator[](size_t idx) const { return (&x)[idx]; }
};