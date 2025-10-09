#pragma once
#include <DirectXMath.h>

using namespace DirectX;

constexpr uint ROOT_SLOT_CBV_PER_FRAME = 0;
constexpr uint ROOT_SLOT_CBV_PER_DRAW = 1;
constexpr uint ROOT_SLOT_SRV_TABLE = 2;

enum CONSTANT_BUFFER_TYPE
{
	CONSTANT_BUFFER_TYPE_PER_FRAME,
	CONSTANT_BUFFER_TYPE_MESH,
	CONSTANT_BUFFER_TYPE_SPRITE,
	CONSTANT_BUFFER_TYPE_ATMOS_CONSTANTS,
	CONSTANT_BUFFER_TYPE_COUNT
};

struct CONSTANT_BUFFER_PROPERTY
{
	CONSTANT_BUFFER_TYPE Type;
	size_t Size;
};

struct CONSTANT_BUFFER_PER_FRAME
{
	Matrix4x4 View;
	Matrix4x4 Proj;
	Matrix4x4 ViewProj;
	Matrix4x4 InvView;
	Matrix4x4 InvProj;
	Matrix4x4 InvViewProj;

	XMFLOAT3 LightDir; // Directional light direction (normalized)
	float _pad0;
	XMFLOAT3 LightColor; // Light color
	float _pad1;
	XMFLOAT3 Ambient; // Ambient light color
	float _pad2;

	float Near;
	float Far;
	uint MaxRadianceRayRecursionDepth;
};

struct CONSTANT_BUFFER_MESH_OBJECT
{
	Matrix4x4 WorldMatrix;
};

struct CONSTANT_BUFFER_SPRITE_OBJECT
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

struct CONSTANT_BUFFER_ATMOS
{
	XMFLOAT3 CameraPosPlanetCoord;
	float _pad0;               // padding for 16B alignment

	XMFLOAT3 SunDir;
	float SunExposure;
	XMFLOAT3 SunIrradiance;   // solar irradiance at TOA
	float _pad1;

	// Radii
	float PlanetRadius;         // Rg
	float AtmosphereHeight;     // H
	float TopRadius;            // Rt = Rg + H
	float _pad2;                // padding (to make next float align 16B)

	// Mie phase
	float  MieG;
	XMFLOAT3 MieTint;           // normalized tint

	// LUT logical sizes (as floats for consistency with HLSL)
	float TW;	// Transmittance size
	float TH;
	float SR;	// Scattering dimensions
	float SMU;
	float SMUS;
	float SNU;
};