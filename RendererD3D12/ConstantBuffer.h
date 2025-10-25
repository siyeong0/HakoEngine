#pragma once
#include <DirectXMath.h>

struct Light
{
	FLOAT3 PosOrDir;
	float Rs;
	FLOAT3 Color;
	LIGHT_TYPE Type;
};

enum CONSTANT_BUFFER_TYPE
{
	CONSTANT_BUFFER_TYPE_PER_FRAME,
	CONSTANT_BUFFER_TYPE_MESH,
	CONSTANT_BUFFER_TYPE_SPRITE,
	CONSTANT_BUFFER_TYPE_ATMOS,
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

	float Near;
	float Far;

	uint MaxRadianceRayRecursionDepth;
	uint MaxShadowRayRecursionDepth;
	uint NumLights;
	uint Reseaved0;
	uint Reseaved1;
	uint Reseaved2;

	Light LightList[MAX_LIGHT_COUNT];
};
static_assert(sizeof(CONSTANT_BUFFER_PER_FRAME) % 16 == 0, "CONSTANT_BUFFER_PER_FRAME size must be multiple of 16 bytes.");
static_assert(sizeof(CONSTANT_BUFFER_PER_FRAME) <= 1024, "CONSTANT_BUFFER_PER_FRAME size must be less than or equal to 1024 bytes.");
static_assert(sizeof(CONSTANT_BUFFER_PER_FRAME) == 
	6 * sizeof(Matrix4x4) + 
	2 * sizeof(float) + 
	6 * sizeof(uint) + 
	MAX_LIGHT_COUNT * sizeof(Light), "CONSTANT_BUFFER_PER_FRAME size mismatch.");

struct CONSTANT_BUFFER_MESH_OBJECT
{
	Matrix4x4 WorldMatrix;
	RenderMaterial Material;
};
static_assert(sizeof(CONSTANT_BUFFER_MESH_OBJECT) % 16 == 0, "CONSTANT_BUFFER_MESH_OBJECT size must be multiple of 16 bytes.");

struct CONSTANT_BUFFER_SPRITE_OBJECT
{
	FLOAT2 ScreenResolution;
	FLOAT2 Position;
	FLOAT2 Scale;
	FLOAT2 TexSize;
	FLOAT2 TexSampePos;
	FLOAT2 TexSampleSize;
	float	Z;
	float	Alpha;
	float	Reserved0;
	float	Reserved1;
};
static_assert(sizeof(CONSTANT_BUFFER_SPRITE_OBJECT) % 16 == 0, "CONSTANT_BUFFER_SPRITE_OBJECT size must be multiple of 16 bytes.");

struct CONSTANT_BUFFER_ATMOS
{
	FLOAT3 CameraPosPlanetCoord;
	float _pad0;               // padding for 16B alignment

	FLOAT3 SunDir;
	float SunExposure;
	FLOAT3 SunIrradiance;   // solar irradiance at TOA
	float _pad1;

	// Radii
	float PlanetRadius;         // Rg
	float AtmosphereHeight;     // H
	float TopRadius;            // Rt = Rg + H
	float _pad2;                // padding (to make next float align 16B)

	// Mie phase
	float  MieG;
	FLOAT3 MieTint;           // normalized tint

	// LUT logical sizes (as floats for consistency with HLSL)
	float TW;	// Transmittance size
	float TH;
	float SR;	// Scattering dimensions
	float SMU;
	float SMUS;
	float SNU;

	float _pad3;
	float _pad4;
};
static_assert(sizeof(CONSTANT_BUFFER_ATMOS) % 16 == 0, "CONSTANT_BUFFER_ATMOS size must be multiple of 16 bytes.");