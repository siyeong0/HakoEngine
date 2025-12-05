#pragma once
#include "Common/Common.h"
#include "D3D12Texture.h"
#include "Shaders/HLSL_CPP_CommonTypes.h"

// Constants
constexpr uint SWAP_CHAIN_FRAME_COUNT = 3;
constexpr uint MAX_PENDING_FRAME_COUNT = SWAP_CHAIN_FRAME_COUNT - 1;

constexpr uint MAX_RENDER_THREAD_COUNT = 8;

constexpr uint MAX_SHADER_NAME_BUFFER_LEN = 256;
constexpr uint MAX_SHADER_NAME_LEN = MAX_SHADER_NAME_BUFFER_LEN - 1;
constexpr uint MAX_SHADER_NUM = 2048;
constexpr uint MAX_CODE_SIZE = (1024 * 1024);

constexpr size_t PAYLOAD_SIZE = 20;
constexpr size_t MAX_TRIGROUP_COUNT_PER_BLAS = 16;

#define SPHERE_ATTRIB_SIZE 12u 

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

struct CBMaterial
{
	FLOAT3 BaseColor = { 0.0f,0.0f,0.0f };
	float Opacity = 0.0f;

	FLOAT3 SpecularColor = { 0.0f,0.0f,0.0f };
	float SpecularFactor = 0.0f;

	float MetallicFactor = 0.0f;
	float RoughnessFactor = 0.0f;
	float NormalScale = 0.0f;
	float AmbientOcclusionStrength = 0.0f;
};

struct CONSTANT_BUFFER_MESH_OBJECT
{
	Matrix4x4 WorldMatrix;
	CBMaterial Material;
};
static_assert(sizeof(CONSTANT_BUFFER_MESH_OBJECT) % 16 == 0, "CONSTANT_BUFFER_MESH_OBJECT size must be multiple of 16 bytes.");

struct CONSTANT_BUFFER_RT_TRIGROUP
{
	CBMaterial Material = {};
};

struct CONSTANT_BUFFER_RT_PROC
{
	uint InstanceIndex = 0;
	float Reserved0 = 0.0f;
	float Reserved1 = 0.0f;
	float Reserved2 = 0.0f;
	CBMaterial Material = {};
};

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

struct FontHandle
{
	IDWriteTextFormat*	pTextFormat;
	float FontSize;
	wchar_t wchFontFamilyName[512];
};

struct ShaderHandle
{
	uint Flags;
	uint CodeSize;
	uint ShaderNameLen;
	wchar_t wchShaderName[MAX_SHADER_NAME_BUFFER_LEN];
	uint CodeBuffer[1];
};

struct MeshSection
{
	ID3D12Resource* IndexBuffer = nullptr;
	D3D12_INDEX_BUFFER_VIEW IndexBufferView = {};
	uint NumTriangles = 0;
	D3D12Texture* DiffuseTexHandle = nullptr;
	D3D12Texture* NormalTexHandle = nullptr;
	CBMaterial Material = {};
};

struct RootArgument
{
	CONSTANT_BUFFER_RT_TRIGROUP Constants = {};
	D3D12_GPU_DESCRIPTOR_HANDLE SrvTable = {}; // (t0..t3, space1)
};

struct BLASHandle
{
	ID3D12Resource* pBLAS;

	uint32_t ID;
	bool bAllowUpdate;
	uint ShaderRecordIndex;
	enum class GeomKind : uint32_t { Triangles, Procedural, Casper };
	GeomKind Kind = GeomKind::Triangles;
	uint NumVertices;
	uint NumTriGroups; // Tri: triGroup 수 / Proc: AABB 개수
	D3D12_RAYTRACING_GEOMETRY_DESC pGeomDescList[MAX_TRIGROUP_COUNT_PER_BLAS] = {};

	// Local params
	D3D12_CPU_DESCRIPTOR_HANDLE SrvCpuHandle;
	D3D12_GPU_DESCRIPTOR_HANDLE SrvGpuHandle;
	std::vector<RootArgument> RootArgArray;
	//std::vector<RootArgumentProc> RootArgProcArray;
};

struct BLASInstance
{
	BLASHandle* pBLASHandle;
	Matrix Transform;
	uint8_t InstanceMask = 0xFF;
	D3D12_RAYTRACING_INSTANCE_FLAGS Flags = D3D12_RAYTRACING_INSTANCE_FLAG_NONE;
};

enum RENDER_PASS_TYPE : uint
{
	RENDER_PASS_OPAQUE,
	RENDER_PASS_TRANSPARENT,
	RENDER_PASS_RAYTRACING,
};

enum RENDER_ITEM_TYPE
{
	RENDER_ITEM_TYPE_MESH_OBJ,
	RENDER_ITEM_TYPE_PROCEDURAL_SPHERE_OBJ,
	RENDER_ITEM_TYPE_CASPER_OBJ,
	RENDER_ITEM_TYPE_SPRITE
};

struct RenderMeshParam
{
	Matrix4x4 WorldMatrix;
};

struct RenderSpriteParam
{
	int PosX;
	int PosY;
	float ScaleX;
	float ScaleY;
	RECT Rect;
	bool bUseRect;
	float Z;
	void* pTexHandle;
};

struct RenderItem
{
	RENDER_ITEM_TYPE Type;
	void* pObjHandle;
	union
	{
		RenderMeshParam MeshObjParam;
		RenderSpriteParam SpriteParam;
	};
};