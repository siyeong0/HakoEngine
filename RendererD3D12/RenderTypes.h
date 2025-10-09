#pragma once
#include "Common/Common.h"
#include <DirectXMath.h>

using namespace DirectX;

constexpr uint SWAP_CHAIN_FRAME_COUNT = 3;
constexpr uint MAX_PENDING_FRAME_COUNT = SWAP_CHAIN_FRAME_COUNT - 1;

const size_t PAYLOAD_SIZE = 20;
const size_t MAX_TRIGROUP_COUNT_PER_BLAS = 16;

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
	WCHAR wchFontFamilyName[512];
};

struct IndexedTriGroup
{
	ID3D12Resource* IndexBuffer = nullptr;
	D3D12_INDEX_BUFFER_VIEW IndexBufferView = {};
	uint NumTriangles = 0;
	TextureHandle* pTexHandle = nullptr;
	bool bOpaque = true;
};

constexpr uint MAX_SHADER_NAME_BUFFER_LEN = 256;
constexpr uint MAX_SHADER_NAME_LEN = MAX_SHADER_NAME_BUFFER_LEN - 1;
constexpr uint MAX_SHADER_NUM = 2048;
constexpr uint MAX_CODE_SIZE = (1024 * 1024);

struct ShaderHandle
{
	uint Flags;
	uint CodeSize;
	uint ShaderNameLen;
	WCHAR wchShaderName[MAX_SHADER_NAME_BUFFER_LEN];
	uint CodeBuffer[1];
};

struct RootArgument
{
	D3D12_GPU_DESCRIPTOR_HANDLE SrvVB;
	D3D12_GPU_DESCRIPTOR_HANDLE SrvIB;
	D3D12_GPU_DESCRIPTOR_HANDLE SrvTexDiffuse;
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