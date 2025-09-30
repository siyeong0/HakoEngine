#pragma once
#include <DirectXMath.h>

using namespace DirectX;

struct CB_PerFrame
{
	XMMATRIX ViewMatrix;
	XMMATRIX ProjMatrix;
	XMMATRIX InvViewMatrix;
};

struct CB_BasicMeshMatrices
{
	XMMATRIX WorldMatrix;
};

struct ConstantBufferSprite
{
	XMFLOAT2 ScreenResolution;
	XMFLOAT2 Position;
	XMFLOAT2 Scale;
	XMFLOAT2 TexSize;
	XMFLOAT2 TexSampePos;
	XMFLOAT2 TexSampleSize;
	float	Z;
	float	Alpha;
	float	Reserved0;
	float	Reserved1;
};


struct CB_SkyMatrices
{
	XMFLOAT4X4 InvProj;
	XMFLOAT4X4 InvView; // 회전만의 inverse view(= camera rotation matrix)
};

struct CB_SkyParams
{
	XMFLOAT3 SunDirW;
	float pad1;
};
