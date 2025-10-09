#pragma once
#include "Common/Common.h"
#include <DirectXMath.h>

using namespace DirectX;

constexpr uint SWAP_CHAIN_FRAME_COUNT = 3;
constexpr uint MAX_PENDING_FRAME_COUNT = SWAP_CHAIN_FRAME_COUNT - 1;

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

// Ray Tracing

const size_t PAYLOAD_SIZE = 20;
const size_t MAX_TRIGROUP_COUNT_PER_BLAS = 16;

struct RootArgument
{
	D3D12_GPU_DESCRIPTOR_HANDLE SrvVB;
	D3D12_GPU_DESCRIPTOR_HANDLE SrvIB;
	D3D12_GPU_DESCRIPTOR_HANDLE SrvTexDiffuse;
};

struct IndexedTriGroup
{
	ID3D12Resource* IndexBuffer = nullptr;
	D3D12_INDEX_BUFFER_VIEW IndexBufferView = {};
	uint NumTriangles = 0;
	TextureHandle* pTexHandle = nullptr;
	bool bOpaque = true;
};

struct BLASInstance
{
	void* pSrcMeshObj;
	ID3D12Resource* pBLAS;
	XMMATRIX Transform;

	uint32_t ID;
	uint ShaderRecordIndex;
	uint NumVertices;
	uint NumTriGroups;

	// Local params
	D3D12_CPU_DESCRIPTOR_HANDLE SrvCpuHandle;
	D3D12_GPU_DESCRIPTOR_HANDLE SrvGpuHandle;
	RootArgument pRootArg[1];
};
