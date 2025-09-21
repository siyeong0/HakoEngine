#pragma once

#include <DirectXMath.h>
#include "Util/LinkedList.h"

using namespace DirectX;

constexpr UINT SWAP_CHAIN_FRAME_COUNT = 3;
constexpr UINT MAX_PENDING_FRAME_COUNT = SWAP_CHAIN_FRAME_COUNT - 1;


struct ConstantBufferDefault
{
	XMMATRIX WorldMatrix;
	XMMATRIX ViewMatrix;
	XMMATRIX ProjMatrix;
};

struct ConstantBufferSprite
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

enum EConstantBufferType
{
	CONSTANT_BUFFER_TYPE_DEFAULT,
	CONSTANT_BUFFER_TYPE_SPRITE,
	CONSTANT_BUFFER_TYPE_COUNT
};

struct ConstantBufferProperty
{
	EConstantBufferType Type;
	size_t Size;
};

struct TextureHandle
{
	ID3D12Resource*	pTexResource;
	ID3D12Resource*	pUploadBuffer;
	D3D12_CPU_DESCRIPTOR_HANDLE SRV;
	bool bUpdated;
	bool bFromFile;
	int RefCount;
	void* pSearchHandle;
	SORT_LINK Link;
};

struct FontHandle
{
	IDWriteTextFormat*	pTextFormat;
	float FontSize;
	WCHAR wchFontFamilyName[512];
};
