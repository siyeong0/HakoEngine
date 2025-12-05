#pragma once
#include "Common/Common.h"
#include "Generic/Color.h"
#include "Generic/Image.h"

struct TEXTURE_DESC;

struct ITexture
{
	enum class FilterMode
	{
		Point,
		Bilinear,
		Trilinear,
	};

	enum class WrapMode
	{
		Repeat,
		Clamp,
		Mirror,
		MirrorOnce,
		Border,
	};

	enum class Dimension
	{
		Tex2D,
		Tex3D,
		Cube,
		Tex2DArray,
		CubeArray,
	};

	virtual ~ITexture() = default;

	virtual bool  ENGINECALL Initialize(const TEXTURE_DESC& desc) = 0;
	virtual void  ENGINECALL Cleanup() = 0;

	virtual void ENGINECALL Apply(bool bUpdateMipMaps = true) = 0;

	virtual Dimension ENGINECALL GetDimension() const = 0;
	virtual uint3 ENGINECALL GetSize(uint mipLevel = 0) const = 0;
	virtual uint ENGINECALL GetMipCount()   const = 0;
	virtual uint ENGINECALL GetArraySize()  const = 0;
	virtual uint ENGINECALL GetSampleCount() const = 0;

	virtual TEXFORMAT ENGINECALL GetFormat() const = 0;
	virtual bool ENGINECALL IsDynamic() const = 0;
	virtual bool ENGINECALL IsDataSRGB() const = 0;
	virtual bool ENGINECALL IsReadable() const = 0;

	virtual FilterMode ENGINECALL GetFilterMode() const = 0;
	virtual void ENGINECALL SetFilterMode(FilterMode mode) = 0;

	virtual WrapMode ENGINECALL GetWrapModeU() const = 0;
	virtual WrapMode ENGINECALL GetWrapModeV() const = 0;
	virtual WrapMode ENGINECALL GetWrapModeW() const = 0;

	virtual void ENGINECALL SetWrapModeU(WrapMode mode) = 0;
	virtual void ENGINECALL SetWrapModeV(WrapMode mode) = 0;
	virtual void ENGINECALL SetWrapModeW(WrapMode mode) = 0;

	virtual uint ENGINECALL GetAnisoLevel() const = 0;
	virtual void ENGINECALL SetAnisoLevel(uint level) = 0;

	virtual float ENGINECALL GetMipMapBias() const = 0;
	virtual void ENGINECALL SetMipMapBias(float bias) = 0;

	virtual void ENGINECALL CopyRegion(
		const void* src,
		size_t srcRowPitchBytes,
		uint3 srcPos,
		uint3 dstPos,
		uint3 extent,
		uint srcMipLevel = 0,
		uint dstMipLevel = 0,
		uint srcArraySlice = 0,
		uint dstArraySlice = 0) = 0;

	virtual void ENGINECALL CopyRegion(const ITexture& src) = 0;

	virtual void ENGINECALL CopyRegion(
		const ITexture& src,
		uint3 srcPos,
		uint3 dstPos,
		uint3 extent,
		uint srcMipLevel = 0,
		uint dstMipLevel = 0,
		uint srcArraySlice = 0,
		uint dstArraySlice = 0) = 0;

	virtual Color ENGINECALL GetPixel(const uint3& pos, uint mipLevel = 0, uint arraySlice = 0) const = 0;
	virtual Color ENGINECALL GetPixelBilinear(const FVector2& uv, uint mipLevel = 0, uint arraySlice = 0) const = 0;
	virtual void  ENGINECALL SetPixel(const uint3& pos, const Color& color, uint mipLevel = 0, uint arraySlice = 0) = 0;
};

struct TEXTURE_DESC
{
	ITexture::Dimension dimension = ITexture::Dimension::Tex2D;
	TEXFORMAT format = TEXFORMAT_RGBA8_UNORM;

	uint3 size = { 1, 1, 1 };
	uint  mipCount = 1;
	uint  arraySize = 1;
	uint  sampleCount = 1;

	bool bReadable = false;
	bool bDataSRGB = false;

	bool bDynamic = false;

	ITexture::FilterMode filterMode = ITexture::FilterMode::Bilinear;
	ITexture::WrapMode   wrapU = ITexture::WrapMode::Repeat;
	ITexture::WrapMode   wrapV = ITexture::WrapMode::Repeat;
	ITexture::WrapMode   wrapW = ITexture::WrapMode::Repeat;

	uint  anisoLevel = 1;
	float mipMapBias = 0.0f;
};
