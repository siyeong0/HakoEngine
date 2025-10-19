#pragma once
#include "Common/Common.h"
#include "Shaders/HLSL_CPP_CommonTypes.h"

// Constants
constexpr uint SWAP_CHAIN_FRAME_COUNT = 3;
constexpr uint MAX_PENDING_FRAME_COUNT = SWAP_CHAIN_FRAME_COUNT - 1;

constexpr uint MAX_SHADER_NAME_BUFFER_LEN = 256;
constexpr uint MAX_SHADER_NAME_LEN = MAX_SHADER_NAME_BUFFER_LEN - 1;
constexpr uint MAX_SHADER_NUM = 2048;
constexpr uint MAX_CODE_SIZE = (1024 * 1024);

constexpr size_t PAYLOAD_SIZE = 20;
constexpr size_t MAX_TRIGROUP_COUNT_PER_BLAS = 16;

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
	FLOAT3 Ks;
	MATERIAL_TYPE Type;
	FLOAT3 Kr;
	float Roughness;
	FLOAT3 Kt;
	float AmbientIntensity;
	FLOAT3 Opacity;
	float Reserved0;
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
	RenderMaterial Material;
};

struct RootArgument
{
	CONSTANT_BUFFER_RT_TRIGROUP Cb;
	D3D12_GPU_DESCRIPTOR_HANDLE SrvVB;
	D3D12_GPU_DESCRIPTOR_HANDLE SrvIB;
	D3D12_GPU_DESCRIPTOR_HANDLE SrvTexDiffuse;
	D3D12_GPU_DESCRIPTOR_HANDLE SrvTexNormal;
};

struct BLASHandle
{
	void* pSrcMeshObj;
	ID3D12Resource* pBLAS;
	Matrix4x4 Transform;

	uint32_t ID;
	uint ShaderRecordIndex;
	uint NumVertices;
	uint NumTriGroups;

	// Local params
	D3D12_CPU_DESCRIPTOR_HANDLE SrvCpuHandle;
	D3D12_GPU_DESCRIPTOR_HANDLE SrvGpuHandle;
	RootArgument pRootArg[1];
};

enum RENDER_PASS_TYPE : uint
{
	RENDER_PASS_OPAQUE,
	RENDER_PASS_TRANSPARENT,
	RENDER_PASS_RAYTRACING_OPAQUE,
	RENDER_PASS_RAYTRACING_TRANSPARENT,
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