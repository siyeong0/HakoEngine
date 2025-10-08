#pragma once
#include <DirectXMath.h>

using namespace DirectX;

struct FLOAT3
{
	float x;
	float y;
	float z;
};

struct FLOAT4
{
	float x;
	float y;
	float z;
	float w;
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

struct Vertex
{
	FLOAT3 Position;
	FLOAT2 TexCoord;
	FLOAT3 Normal;
	FLOAT3 Tangent;
};
static_assert(sizeof(Vertex) == 44, "Vertex size mismatch");

struct SpriteVertex
{
	XMFLOAT3 Position;
	XMFLOAT4 Color;
	XMFLOAT2 TexCoord;
};
static_assert(sizeof(SpriteVertex) == 36, "SpriteVertex size mismatch");
