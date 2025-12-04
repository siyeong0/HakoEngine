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

    enum class Format
    {
        Unknown,

        RGBA8_UNorm,
        RGBA8_UNorm_SRGB,
        BGRA8_UNorm,
        BGRA8_UNorm_SRGB,
        RGBA16_Float,
        RGBA32_Float,

        D24_UNorm_S8_UInt,
        D32_Float,
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

    // CPU-side 변경사항을 GPU에 업로드 (동시에 MipMap 재생성 옵션)
    virtual void ENGINECALL Apply(bool bUpdateMipMaps = true) = 0;

    // 기본 정보
    virtual Dimension ENGINECALL GetDimension() const = 0;
    virtual uint3     ENGINECALL GetSize(uint mipLevel = 0) const = 0;
    virtual uint      ENGINECALL GetMipCount()   const = 0;
    virtual uint      ENGINECALL GetArraySize()  const = 0;
    virtual uint      ENGINECALL GetSampleCount() const = 0; // MSAA: 1 = no MSAA

    virtual Format    ENGINECALL GetFormat() const = 0;
    virtual bool      ENGINECALL IsDataSRGB() const = 0;
    virtual bool      ENGINECALL IsReadable() const = 0;

    // 샘플링 상태
    virtual FilterMode ENGINECALL GetFilterMode() const = 0;
    virtual void       ENGINECALL SetFilterMode(FilterMode mode) = 0;

    virtual WrapMode   ENGINECALL GetWrapModeU() const = 0;
    virtual WrapMode   ENGINECALL GetWrapModeV() const = 0;
    virtual WrapMode   ENGINECALL GetWrapModeW() const = 0; // ★ 추가 (3D / Array 대비)

    virtual void       ENGINECALL SetWrapModeU(WrapMode mode) = 0;
    virtual void       ENGINECALL SetWrapModeV(WrapMode mode) = 0;
    virtual void       ENGINECALL SetWrapModeW(WrapMode mode) = 0;

    virtual uint       ENGINECALL GetAnisoLevel() const = 0;
    virtual void       ENGINECALL SetAnisoLevel(uint level) = 0;

    virtual float      ENGINECALL GetMipMapBias() const = 0;
    virtual void       ENGINECALL SetMipMapBias(float bias) = 0;

    // Raw 메모리에서 복사
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

    // 텍스처끼리 복사 (전체)
    virtual void ENGINECALL CopyRegion(const ITexture& src) = 0;

    // 텍스처끼리 복사 (부분)
    virtual void ENGINECALL CopyRegion(
        const ITexture& src,
        uint3 srcPos,
        uint3 dstPos,
        uint3 extent,
        uint srcMipLevel = 0,
        uint dstMipLevel = 0,
        uint srcArraySlice = 0,
        uint dstArraySlice = 0) = 0;

    // CPU 샘플링/쓰기 (IsReadable == true일 때만 의미 있음)
    virtual Color ENGINECALL GetPixel(const uint3& pos, uint mipLevel = 0, uint arraySlice = 0) const = 0;
    virtual Color ENGINECALL GetPixelBilinear(const FVector2& uv, uint mipLevel = 0, uint arraySlice = 0) const = 0;
    virtual void  ENGINECALL SetPixel(const uint3& pos, const Color& color, uint mipLevel = 0, uint arraySlice = 0) = 0;
};

struct TEXTURE_DESC
{
    ITexture::Dimension dimension = ITexture::Dimension::Tex2D;
    ITexture::Format    format = ITexture::Format::RGBA8_UNorm;

    uint3 size = { 1, 1, 1 }; // (width, height, depth or 1)
    uint  mipCount = 1;
    uint  arraySize = 1;
    uint  sampleCount = 1;

    bool  bReadable = false; // CPU에서 GetPixel/SetPixel 사용할지
    bool  bDataSRGB = false;

    // 동적 텍스처인지 여부 (매 프레임 업데이트 같은 용도)
    bool  bDynamic = false; // ★ TextureHandle의 dynamic/immutable 구분 역할

    ITexture::FilterMode filterMode = ITexture::FilterMode::Bilinear;
    ITexture::WrapMode   wrapU = ITexture::WrapMode::Repeat;
    ITexture::WrapMode   wrapV = ITexture::WrapMode::Repeat;
    ITexture::WrapMode   wrapW = ITexture::WrapMode::Repeat; // ★ 추가

    uint  anisoLevel = 1;
    float mipMapBias = 0.0f;
};
