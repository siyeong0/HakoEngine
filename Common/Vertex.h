#pragma once
#include <DirectXMath.h>

using namespace DirectX;

struct FLOAT3
{
	float x;
	float y;
	float z;
};

struct FLOAT2
{
	union
	{
		struct
		{
			float x;
			float y;
		};
		struct
		{
			float u;
			float v;
		};
	};
};

struct SpriteVertex
{
	XMFLOAT3 Position;
	XMFLOAT4 Color;
	XMFLOAT2 TexCoord;
};
static_assert(sizeof(SpriteVertex) == 36, "SpriteVertex size mismatch");

struct BasicVertex
{
	XMFLOAT3 Position;
	XMFLOAT4 Color;
	XMFLOAT2 TexCoord;
};
static_assert(sizeof(BasicVertex) == 36, "BasicVertex size mismatch");