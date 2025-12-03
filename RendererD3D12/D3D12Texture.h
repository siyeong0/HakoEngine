#pragma once
#include "Interface/ITexture.h"

class D3D12Texture : public ITexture
{
public:
	bool ENGINECALL Initialize(const TEXTURE_DESC& desc) override;
	void ENGINECALL Cleanup() override;
	void ENGINECALL Apply(bool bUpdateMipMaps = true) override;

	ITexture::Dimension ENGINECALL GetDimension() const override { return m_Params.dimension; }
	uint3 ENGINECALL GetSize(uint mipLevel = 0) const override;
	uint ENGINECALL GetMipCount() const override { return m_Params.mipCount; }
	uint ENGINECALL GetArraySize() const override { return m_Params.arraySize; }
	uint ENGINECALL GetSampleCount() const override { return m_Params.sampleCount; }

	ITexture::Format ENGINECALL GetFormat() const override { return m_Params.format; }
	bool ENGINECALL IsDataSRGB() const override { return m_Params.bDataSRGB; }
	bool ENGINECALL IsReadable() const override { return m_Params.bReadable; }

	ITexture::FilterMode ENGINECALL GetFilterMode() const override { return m_Params.filterMode; }
	void ENGINECALL SetFilterMode(ITexture::FilterMode mode) override;

	ITexture::WrapMode ENGINECALL GetWrapModeU() const override { return m_Params.wrapU; }
	ITexture::WrapMode ENGINECALL GetWrapModeV() const override { return m_Params.wrapV; }
	void ENGINECALL SetWrapModeU(ITexture::WrapMode mode) override;
	void ENGINECALL SetWrapModeV(ITexture::WrapMode mode) override;

	uint ENGINECALL GetAnisoLevel() const override { return m_Params.anisoLevel; }
	void ENGINECALL SetAnisoLevel(uint level) override;

	float ENGINECALL GetMipMapBias() const override { return m_Params.mipMapBias; }
	void ENGINECALL SetMipMapBias(float bias) override;

	void ENGINECALL CopyRegion(
		const void* src,
		size_t srcRowPitchBytes,
		uint3 srcPos,
		uint3 dstPos,
		uint3 extent,
		uint srcMipLevel = 0,
		uint dstMipLevel = 0,
		uint srcArraySlice = 0,
		uint dstArraySlice = 0) override;

	void ENGINECALL CopyRegion(const ITexture& src) override;

	void ENGINECALL CopyRegion(
		const ITexture& src,
		uint3 srcPos,
		uint3 dstPos,
		uint3 extent,
		uint srcMipLevel = 0,
		uint dstMipLevel = 0,
		uint srcArraySlice = 0,
		uint dstArraySlice = 0) override;

	Color ENGINECALL GetPixel(const uint3& pos, uint mipLevel = 0, uint arraySlice = 0) const override;
	Color ENGINECALL GetPixelBilinear(const FVector2& uv, uint mipLevel = 0, uint arraySlice = 0) const override;
	void  ENGINECALL SetPixel(const uint3& pos, const Color& color, uint mipLevel = 0, uint arraySlice = 0) override;

	D3D12Texture() = default;
	~D3D12Texture() { Cleanup(); };

	ID3D12Resource* GetD3DResource() const { return m_pTexResource; }
	D3D12_CPU_DESCRIPTOR_HANDLE GetSRV() const { return m_SRV; }
	D3D12_SRV_DIMENSION GetSRVDimension() const { return m_Dimension; }

private:
	DXGI_FORMAT cvtToDXGIFormat(ITexture::Format fmt) const;
	D3D12_SRV_DIMENSION calcSrvDimension(ITexture::Dimension dim) const;
	uint3 calcMipSize(uint mipLevel) const;

private:
	ID3D12Resource* m_pTexResource;
	ID3D12Resource* m_pUploadBuffer;
	D3D12_CPU_DESCRIPTOR_HANDLE m_SRV;
	D3D12_SRV_DIMENSION m_Dimension;

	TEXTURE_DESC m_Params{};
	bool m_bInitialized = false;
	bool m_bDirty = false;
};