#pragma once
#include <DirectXMath.h>

using namespace DirectX;

constexpr UINT ROOT_SLOT_CBV_PER_FRAME = 0;
constexpr UINT ROOT_SLOT_CBV_PER_DRAW = 1;
constexpr UINT ROOT_SLOT_SRV_TABLE = 2;

enum EConstantBufferType
{
	CONSTANT_BUFFER_TYPE_PER_FRAME,
	CONSTANT_BUFFER_TYPE_MESH,
	CONSTANT_BUFFER_TYPE_SPRITE,
	CONSTANT_BUFFER_TYPE_ATMOS_CONSTANTS,
	CONSTANT_BUFFER_TYPE_COUNT
};

struct ConstantBufferProperty
{
	EConstantBufferType Type;
	size_t Size;
};

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

struct CB_MeshObject
{
	XMMATRIX WorldMatrix;
};

struct CB_SpriteObject
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

struct CB_AtmosConstants
{
	XMFLOAT3 CameraPosPlanetCS;
	float _pad0;               // padding for 16B alignment

	XMFLOAT3 SunDirW;
	float SunExposure;

	// Radii
	float PlanetRadius;         // Rg
	float AtmosphereHeight;     // H
	float TopRadius;            // Rt = Rg + H
	float _pad1;                  // padding (to make next float align 16B)

	// Mie phase
	XMFLOAT3 SunIrradiance;   // solar irradiance at TOA
	float  MieG;
	XMFLOAT3 MieTint;           // normalized tint
	float _pad2;

	// LUT logical sizes (as floats for consistency with HLSL)
	float TW;	// Transmittance size
	float TH;
	float SR;	// Scattering dimensions
	float SMU;
	float SMUS;
	float SNU;
};