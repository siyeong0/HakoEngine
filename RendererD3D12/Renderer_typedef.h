#pragma once

#include <DirectXMath.h>

using namespace DirectX;

constexpr UINT SWAP_CHAIN_FRAME_COUNT = 3;
constexpr UINT MAX_PENDING_FRAME_COUNT = SWAP_CHAIN_FRAME_COUNT - 1;

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
};

struct FontHandle
{
	IDWriteTextFormat*	pTextFormat;
	float FontSize;
	WCHAR wchFontFamilyName[512];
};
