#pragma once
#include "Interface/ITexture.h"
#include "D3D12ResourceManager.h"
#include "D3D12Renderer.h" // 또는 전방선언 + 필요 시 .cpp에서 include

class SingleDescriptorAllocator;

class D3D12Texture : public ITexture
{
public:
    // ITexture 인터페이스 구현
    bool ENGINECALL Initialize(const TEXTURE_DESC& desc) override;
    void ENGINECALL Cleanup() override;
    void ENGINECALL Apply(bool bUpdateMipMaps = true) override;

    Dimension ENGINECALL GetDimension() const override { return m_Params.dimension; }
    uint3     ENGINECALL GetSize(uint mipLevel = 0) const override;
    uint      ENGINECALL GetMipCount()   const override { return m_Params.mipCount; }
    uint      ENGINECALL GetArraySize()  const override { return m_Params.arraySize; }
    uint      ENGINECALL GetSampleCount() const override { return m_Params.sampleCount; }

    Format    ENGINECALL GetFormat() const override { return m_Params.format; }
    bool      ENGINECALL IsDataSRGB() const override { return m_Params.bDataSRGB; }
    bool      ENGINECALL IsReadable() const override { return m_Params.bReadable; }

    FilterMode ENGINECALL GetFilterMode() const override { return m_Params.filterMode; }
    void       ENGINECALL SetFilterMode(FilterMode mode) override;

    WrapMode   ENGINECALL GetWrapModeU() const override { return m_Params.wrapU; }
    WrapMode   ENGINECALL GetWrapModeV() const override { return m_Params.wrapV; }
    WrapMode   ENGINECALL GetWrapModeW() const override { return m_Params.wrapW; }

    void       ENGINECALL SetWrapModeU(WrapMode mode) override;
    void       ENGINECALL SetWrapModeV(WrapMode mode) override;
    void       ENGINECALL SetWrapModeW(WrapMode mode) override;

    uint       ENGINECALL GetAnisoLevel() const override { return m_Params.anisoLevel; }
    void       ENGINECALL SetAnisoLevel(uint level) override;

    float      ENGINECALL GetMipMapBias() const override { return m_Params.mipMapBias; }
    void       ENGINECALL SetMipMapBias(float bias) override;

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

    // renderer / resource manager / descriptor allocator 주입
    D3D12Texture(
        ID3D12Device5* pDevice,
        D3D12ResourceManager* pResourceManager,
        SingleDescriptorAllocator* pSrvAllocator)
        : m_pDevice(pDevice)
        , m_pResourceManager(pResourceManager)
        , m_pSrvAllocator(pSrvAllocator)
    {
    }

    ~D3D12Texture() override
    {
        Cleanup();
    }

    // D3D12 전용 helper
    ID3D12Resource* GetD3DResource() const { return m_pTexResource; }
    ID3D12Resource* GetUploadBuffer() const { return m_pUploadBuffer; }
    D3D12_CPU_DESCRIPTOR_HANDLE GetSRV() const { return m_SRV; }
    D3D12_SRV_DIMENSION        GetSRVDimension() const { return m_SrvDimension; }

    const TEXTURE_DESC& GetDesc() const { return m_Params; }

private:
    DXGI_FORMAT         cvtToDXGIFormat(ITexture::Format fmt) const;
    D3D12_SRV_DIMENSION calcSrvDimension(ITexture::Dimension dim, uint arraySize, uint sampleCount) const;
    uint3               calcMipSize(uint mipLevel) const;

    void                createTextureResource();
    void                createUploadBufferForDynamic();
    void                createSRV();

    // CPU-side 읽기/쓰기를 위한 임시 스토리지 (bReadable == true 경우에만 사용)
    Color& cpuPixelRef(const uint3& pos, uint mipLevel, uint arraySlice);
    const Color& cpuPixelRef(const uint3& pos, uint mipLevel, uint arraySlice) const;

private:
    ID3D12Device5* m_pDevice = nullptr;
    D3D12ResourceManager* m_pResourceManager = nullptr;
    SingleDescriptorAllocator* m_pSrvAllocator = nullptr;

    ID3D12Resource* m_pTexResource = nullptr; // GPU 텍스처
    ID3D12Resource* m_pUploadBuffer = nullptr; // 동적 업데이트용 업로드 버퍼 (동적 텍스처일 때)
    D3D12_CPU_DESCRIPTOR_HANDLE m_SRV = {};
    D3D12_SRV_DIMENSION        m_SrvDimension = D3D12_SRV_DIMENSION_UNKNOWN;

    TEXTURE_DESC               m_Params{};
    bool                       m_bInitialized = false;
    bool                       m_bDirtyGPU = false;   // 업로드 버퍼 → GPU로 복사가 필요한지
    bool                       m_bSamplerDirty = false;   // 필터/래핑 상태 변경됨

    // 간단한 CPU-side 텍스처 버퍼 (IsReadable == true일 때만 사용)
    std::vector<Color>         m_SystemMemory;     // 모든 mip * arraySlice에 대해 Color 저장
};
