#pragma once
#include "Common/Common.h"

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
	FLOAT3 Position;
	FLOAT4 Color;
	FLOAT2 TexCoord;
};
static_assert(sizeof(SpriteVertex) == 36, "SpriteVertex size mismatch");
