#pragma once
#include "Common/Common.h"
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

struct TextureHandle
{
	ID3D12Resource*	pTexResource;
	ID3D12Resource*	pUploadBuffer;
	D3D12_CPU_DESCRIPTOR_HANDLE SRV;
	D3D12_SRV_DIMENSION Dimension;
	bool bUpdated;
	bool bFromFile;
	int RefCount;
};

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

struct RenderMaterial
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

struct IndexedTriGroup
{
	ID3D12Resource* IndexBuffer = nullptr;
	D3D12_INDEX_BUFFER_VIEW IndexBufferView = {};
	uint NumTriangles = 0;
	TextureHandle* DiffuseTexHandle = nullptr;
	TextureHandle* NormalTexHandle = nullptr;
	RenderMaterial Material = {};
};

struct CONSTANT_BUFFER_RT_TRIGROUP
{
	RenderMaterial Material = {};
};

struct CONSTANT_BUFFER_RT_PROC
{
	uint InstanceIndex = 0;
	float Reserved0 = 0.0f;
	float Reserved1 = 0.0f;
	float Reserved2 = 0.0f;
	RenderMaterial Material = {};
};

struct RootArgument
{
	CONSTANT_BUFFER_RT_TRIGROUP Constants = {};
	D3D12_GPU_DESCRIPTOR_HANDLE SrvTable = {}; // (t0..t3, space1)
};

struct RootArgumentProc
{
	CONSTANT_BUFFER_RT_PROC Constants = {};
	D3D12_GPU_DESCRIPTOR_HANDLE SrvTable = {}; // (t0..t3, space1)
};

struct BLASHandle
{
	ID3D12Resource* pBLAS;

	uint32_t ID;
	bool bAllowUpdate;
	uint ShaderRecordIndex;
	enum class GeomKind : uint32_t { Triangles, Procedural };
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
	Matrix4x4 Transform;
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