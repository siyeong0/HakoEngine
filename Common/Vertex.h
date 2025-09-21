#pragma once
#include <DirectXMath.h>

using namespace DirectX;

struct FLOAT3
{
	float x;
	float y;
	float z;
};

struct BasicVertex
{
	XMFLOAT3 position;
	XMFLOAT4 color;
	XMFLOAT2 texCoord;
};

struct TVERTEX
{
	float u;
	float v;
};