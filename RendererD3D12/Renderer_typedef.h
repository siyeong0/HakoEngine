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

const UINT PAYLOAD_SIZE = 20;
const DWORD MAX_TRIGROUP_COUNT_PER_BLAS = 16;

struct ROOT_ARG
{
	D3D12_GPU_DESCRIPTOR_HANDLE srvVB;
	D3D12_GPU_DESCRIPTOR_HANDLE srvIB;
	D3D12_GPU_DESCRIPTOR_HANDLE srvTexDiffuse;
};

struct BLAS_BUILD_TRIGROUP_INFO
{
	ID3D12Resource* pIB;
	ID3D12Resource* pTexResource;
	DWORD	dwIndexNum;
	BOOL	bNotOpaque;
};

struct BLAS_INSTANCE
{
	void* pSrcMeshObj;
	ID3D12Resource* pBLAS;
	XMMATRIX matTransform;

	DWORD	dwID;
	UINT	ShaderRecordIndex;
	DWORD	dwVertexCount;
	DWORD	dwTriGroupCount;
};
