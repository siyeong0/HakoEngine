#pragma once
#include <DirectXMath.h>

using namespace DirectX;

struct CB_PerFrame
{
	XMMATRIX ViewMatrix;
	XMMATRIX ProjMatrix;
	XMMATRIX ViewProjMatrix;
	XMMATRIX InvViewMatrix;
	XMMATRIX InvProjMatrix;
	XMMATRIX InvViewProjMatrix;

	XMFLOAT3 LightDir; // Directional light direction (normalized)
	float _pad0;
	XMFLOAT3 LightColor; // Light color
	float _pad1;
	XMFLOAT3 Ambient; // Ambient light color
	float _pad2;
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
