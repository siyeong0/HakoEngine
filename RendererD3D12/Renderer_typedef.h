#pragma once

#include <DirectXMath.h>

using namespace DirectX;

constexpr UINT SWAP_CHAIN_FRAME_COUNT = 3;
constexpr UINT MAX_PENDING_FRAME_COUNT = SWAP_CHAIN_FRAME_COUNT - 1;

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

struct BLASBuilTriGroupInfo
{
	ID3D12Resource* pIB;
	ID3D12Resource* pTexResource;
	UINT TexWidth;
	UINT TexHeight;
	DXGI_FORMAT TexFormat;
	UINT NumIndices;
	bool bNotOpaque;
};

struct BLASInstance
{
	void* pSrcMeshObj;
	ID3D12Resource* pBLAS;
	XMMATRIX Transform;

	uint32_t ID;
	UINT ShaderRecordIndex;
	UINT NumVertices;
	UINT NumTriGroups;

	// Local params
	D3D12_CPU_DESCRIPTOR_HANDLE SrvCpuHandle;
	D3D12_GPU_DESCRIPTOR_HANDLE SrvGpuHandle;
	RootArgument pRootArg[1];
};
