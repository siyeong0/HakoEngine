#include "pch.h"
#include "Common/Common.h"
#include "D3D12Renderer.h"
#include "SingleDescriptorAllocator.h"
#include "D3D12Texture.h"

D3D12_SRV_DIMENSION D3D12Texture::calcSrvDimension(
    ITexture::Dimension dim,
    uint arraySize,
    uint sampleCount) const
{
    switch (dim)
    {
    case Dimension::Tex2D:
        return (sampleCount > 1) ? D3D12_SRV_DIMENSION_TEXTURE2DMS : D3D12_SRV_DIMENSION_TEXTURE2D;
    case Dimension::Tex3D:
        return D3D12_SRV_DIMENSION_TEXTURE3D;
    case Dimension::Cube:
        return D3D12_SRV_DIMENSION_TEXTURECUBE;
    case Dimension::Tex2DArray:
        return (sampleCount > 1) ? D3D12_SRV_DIMENSION_TEXTURE2DMSARRAY : D3D12_SRV_DIMENSION_TEXTURE2DARRAY;
    case Dimension::CubeArray:
        return D3D12_SRV_DIMENSION_TEXTURECUBEARRAY;
    default:
        return D3D12_SRV_DIMENSION_UNKNOWN;
    }
}

uint3 D3D12Texture::calcMipSize(uint mipLevel) const
{
    uint3 base = m_Params.size;
    uint shift = std::min(mipLevel, m_Params.mipCount - 1);
    return {
        std::max(1u, base.x >> shift),
        std::max(1u, base.y >> shift),
        std::max(1u, base.z >> shift)
    };
}

void D3D12Texture::createTextureResource()
{
    ASSERT(m_pDevice != nullptr, "D3D12Texture: m_pDevice is null.");
    DXGI_FORMAT dxgiFmt = static_cast<DXGI_FORMAT>(m_Params.format);
    ASSERT(dxgiFmt != DXGI_FORMAT_UNKNOWN, "D3D12Texture: Unknown DXGI_FORMAT.");

    // 여기선 가장 많이 쓰는 케이스(2D, array, cube)를 우선 지원
    const bool b3D = (m_Params.dimension == Dimension::Tex3D);
    const bool bDepth = (dxgiFmt == DXGI_FORMAT_D24_UNORM_S8_UINT || dxgiFmt == DXGI_FORMAT_D32_FLOAT);

    D3D12_RESOURCE_DESC texDesc{};
    if (b3D)
    {
        texDesc = CD3DX12_RESOURCE_DESC::Tex3D(
            dxgiFmt,
            static_cast<UINT64>(m_Params.size.x),
            static_cast<UINT>(m_Params.size.y),
            static_cast<UINT16>(m_Params.size.z),
            static_cast<UINT16>(m_Params.mipCount));
    }
    else
    {
        texDesc = CD3DX12_RESOURCE_DESC::Tex2D(
            dxgiFmt,
            static_cast<UINT64>(m_Params.size.x),
            static_cast<UINT>(m_Params.size.y),
            static_cast<UINT16>(m_Params.arraySize),
            static_cast<UINT16>(m_Params.mipCount),
            static_cast<UINT>(m_Params.sampleCount),
            0);
    }

    D3D12_RESOURCE_STATES initialState = D3D12_RESOURCE_STATE_COMMON;
    D3D12_HEAP_TYPE heapType = D3D12_HEAP_TYPE_DEFAULT;

    // 동적 텍스처라 해도 텍스처 자체는 DEFAULT에 두고, 업로드 버퍼를 따로 둔다.
    HRESULT hr = m_pDevice->CreateCommittedResource(
        &CD3DX12_HEAP_PROPERTIES(heapType),
        D3D12_HEAP_FLAG_NONE,
        &texDesc,
        initialState,
        nullptr,
        IID_PPV_ARGS(&m_pTexResource));

    ASSERT(SUCCEEDED(hr), "D3D12Texture: CreateCommittedResource (texture) failed.");
}

void D3D12Texture::createUploadBufferForDynamic()
{
    if (!m_Params.bDynamic)
        return;

    ASSERT(m_pTexResource != nullptr, "Upload buffer creation requires texture resource.");

    // ResourceManager의 CreateTexturePair를 써도 되지만,
    // 여기서는 "업로드 전용 버퍼"만 만들고, CopyRegion에서 직접 Map/Unmap 하는 패턴으로.
    const auto desc = m_pTexResource->GetDesc();

    UINT64 uploadSize = 0;
    m_pDevice->GetCopyableFootprints(
        &desc,
        0,
        desc.MipLevels * desc.DepthOrArraySize,
        0,
        nullptr,
        nullptr,
        nullptr,
        &uploadSize);

    HRESULT hr = m_pDevice->CreateCommittedResource(
        &CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD),
        D3D12_HEAP_FLAG_NONE,
        &CD3DX12_RESOURCE_DESC::Buffer(uploadSize),
        D3D12_RESOURCE_STATE_GENERIC_READ,
        nullptr,
        IID_PPV_ARGS(&m_pUploadBuffer));

    ASSERT(SUCCEEDED(hr), "D3D12Texture: CreateCommittedResource (upload buffer) failed.");
}

void D3D12Texture::createSRV()
{
    ASSERT(m_pTexResource != nullptr, "Cannot create SRV without texture resource.");
    ASSERT(m_pSrvAllocator != nullptr, "D3D12Texture: m_pSrvAllocator is null.");

    DXGI_FORMAT dxgiFmt = static_cast<DXGI_FORMAT>(m_Params.format);

    D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc{};
    srvDesc.Format = dxgiFmt;
    srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;

    m_SrvDimension = calcSrvDimension(m_Params.dimension, m_Params.arraySize, m_Params.sampleCount);
    srvDesc.ViewDimension = m_SrvDimension;

    switch (m_SrvDimension)
    {
    case D3D12_SRV_DIMENSION_TEXTURE2D:
        srvDesc.Texture2D.MostDetailedMip = 0;
        srvDesc.Texture2D.MipLevels = m_Params.mipCount;
        srvDesc.Texture2D.ResourceMinLODClamp = 0.0f;
        break;
    case D3D12_SRV_DIMENSION_TEXTURE2DARRAY:
        srvDesc.Texture2DArray.MostDetailedMip = 0;
        srvDesc.Texture2DArray.MipLevels = m_Params.mipCount;
        srvDesc.Texture2DArray.FirstArraySlice = 0;
        srvDesc.Texture2DArray.ArraySize = m_Params.arraySize;
        srvDesc.Texture2DArray.ResourceMinLODClamp = 0.0f;
        break;
    case D3D12_SRV_DIMENSION_TEXTURE3D:
        srvDesc.Texture3D.MostDetailedMip = 0;
        srvDesc.Texture3D.MipLevels = m_Params.mipCount;
        srvDesc.Texture3D.ResourceMinLODClamp = 0.0f;
        break;
    case D3D12_SRV_DIMENSION_TEXTURECUBE:
        srvDesc.TextureCube.MostDetailedMip = 0;
        srvDesc.TextureCube.MipLevels = m_Params.mipCount;
        srvDesc.TextureCube.ResourceMinLODClamp = 0.0f;
        break;
    case D3D12_SRV_DIMENSION_TEXTURECUBEARRAY:
        srvDesc.TextureCubeArray.MostDetailedMip = 0;
        srvDesc.TextureCubeArray.MipLevels = m_Params.mipCount;
        srvDesc.TextureCubeArray.First2DArrayFace = 0;
        srvDesc.TextureCubeArray.NumCubes = m_Params.arraySize / 6;
        srvDesc.TextureCubeArray.ResourceMinLODClamp = 0.0f;
        break;
    default:
        ASSERT(false, "Unsupported SRV dimension.");
        break;
    }

    bool bAlloc = m_pSrvAllocator->AllocDescriptorHandle(&m_SRV);
	ASSERT(bAlloc, "D3D12Texture: Failed to allocate SRV descriptor handle.");
    m_pDevice->CreateShaderResourceView(m_pTexResource, &srvDesc, m_SRV);
}

// -----------------------------
// Initialize / Cleanup / Apply
// -----------------------------

bool D3D12Texture::Initialize(const TEXTURE_DESC& desc)
{
    ASSERT(!m_bInitialized, "D3D12Texture already initialized.");
    ASSERT(m_pDevice != nullptr, "D3D12Texture: m_pDevice is null.");
    ASSERT(m_pResourceManager != nullptr, "D3D12Texture: m_pResourceManager is null.");
    ASSERT(m_pSrvAllocator != nullptr, "D3D12Texture: m_pSrvAllocator is null.");

    m_Params = desc;

    createTextureResource();
    createUploadBufferForDynamic();
    createSRV();

    // CPU-side 읽기용 버퍼
    if (m_Params.bReadable)
    {
        // 모든 mip * arraySlice * (width*height*depth) 컬러 저장
        size_t totalPixels = 0;
        for (uint mip = 0; mip < m_Params.mipCount; ++mip)
        {
            uint3 sz = calcMipSize(mip);
            totalPixels += static_cast<size_t>(sz.x) * sz.y * sz.z * m_Params.arraySize;
        }
        m_SystemMemory.resize(totalPixels);
    }

    m_bInitialized = true;
    m_bDirtyGPU = false;
    m_bSamplerDirty = false;
    return true;
}

void D3D12Texture::Cleanup()
{
    if (!m_bInitialized)
    {
        return;
    }

    SAFE_RELEASE(m_pTexResource);
    SAFE_RELEASE(m_pUploadBuffer);
    if (m_SRV.ptr != 0 && m_pSrvAllocator)
    {
        m_pSrvAllocator->FreeDescriptorHandle(m_SRV);
        m_SRV = {};
    }

    m_SystemMemory.clear();

    m_SRV.ptr = 0;
    m_SrvDimension = D3D12_SRV_DIMENSION_UNKNOWN;

    m_bInitialized = false;
    m_bDirtyGPU = false;
    m_bSamplerDirty = false;
}

void D3D12Texture::Apply(bool /*bUpdateMipMaps*/)
{
    if (!m_Params.bDynamic || !m_bDirtyGPU || m_pUploadBuffer == nullptr || m_pTexResource == nullptr)
    {
        return;
    }

    m_pResourceManager->UpdateTextureForWrite(m_pTexResource, m_pUploadBuffer);
    m_bDirtyGPU = false;
}

// -----------------------------
// Getter / Setter for sampling
// -----------------------------

void D3D12Texture::SetFilterMode(FilterMode mode)
{
    if (m_Params.filterMode == mode)
        return;
    m_Params.filterMode = mode;
    m_bSamplerDirty = true; // 실제 sampler는 PSO / sampler heap 관리 쪽에서 반영
}

void D3D12Texture::SetWrapModeU(WrapMode mode)
{
    if (m_Params.wrapU == mode)
        return;
    m_Params.wrapU = mode;
    m_bSamplerDirty = true;
}

void D3D12Texture::SetWrapModeV(WrapMode mode)
{
    if (m_Params.wrapV == mode)
        return;
    m_Params.wrapV = mode;
    m_bSamplerDirty = true;
}

void D3D12Texture::SetWrapModeW(WrapMode mode)
{
    if (m_Params.wrapW == mode)
        return;
    m_Params.wrapW = mode;
    m_bSamplerDirty = true;
}

void D3D12Texture::SetAnisoLevel(uint level)
{
    if (m_Params.anisoLevel == level)
        return;
    m_Params.anisoLevel = level;
    m_bSamplerDirty = true;
}

void D3D12Texture::SetMipMapBias(float bias)
{
    if (m_Params.mipMapBias == bias)
        return;
    m_Params.mipMapBias = bias;
    m_bSamplerDirty = true;
}

// -----------------------------
// CPU-side 메모리 인덱싱
// -----------------------------

Color& D3D12Texture::cpuPixelRef(const uint3& pos, uint mipLevel, uint arraySlice)
{
    ASSERT(m_Params.bReadable, "CPU-readable not enabled for this texture.");
    ASSERT(mipLevel < m_Params.mipCount, "Invalid mip level.");
    ASSERT(arraySlice < m_Params.arraySize, "Invalid array slice.");

    size_t offset = 0;
    for (uint mip = 0; mip < mipLevel; ++mip)
    {
        uint3 sz = calcMipSize(mip);
        offset += static_cast<size_t>(sz.x) * sz.y * sz.z * m_Params.arraySize;
    }

    uint3 sz = calcMipSize(mipLevel);
    ASSERT(pos.x < sz.x && pos.y < sz.y && pos.z < sz.z, "cpuPixelRef out of range.");

    size_t sliceOffset = static_cast<size_t>(arraySlice) * sz.x * sz.y * sz.z;
    size_t idx = offset + sliceOffset
        + static_cast<size_t>(pos.z) * sz.x * sz.y
        + static_cast<size_t>(pos.y) * sz.x
        + pos.x;

    return m_SystemMemory[idx];
}

const Color& D3D12Texture::cpuPixelRef(const uint3& pos, uint mipLevel, uint arraySlice) const
{
    return const_cast<D3D12Texture*>(this)->cpuPixelRef(pos, mipLevel, arraySlice);
}

// -----------------------------
// CopyRegion 구현
// -----------------------------

uint3 D3D12Texture::GetSize(uint mipLevel) const
{
    return calcMipSize(mipLevel);
}

void D3D12Texture::CopyRegion(
    const void* src,
    size_t srcRowPitchBytes,
    uint3 srcPos,
    uint3 dstPos,
    uint3 extent,
    uint srcMipLevel,
    uint dstMipLevel,
    uint /*srcArraySlice*/,
    uint dstArraySlice)
{
    ASSERT(m_pUploadBuffer != nullptr, "CopyRegion requires upload buffer (dynamic texture).");
    ASSERT(src != nullptr, "src must not be null.");

    // 현재 구현은 "전체 영역 업데이트"만 지원하는 셈으로 단순화
    // 필요하면 footprint 계산해서 부분 업데이트 가능.
    ASSERT(srcPos.x == 0 && srcPos.y == 0 && srcPos.z == 0, "Only full-region updates supported (srcPos must be 0).");
    ASSERT(dstPos.x == 0 && dstPos.y == 0 && dstPos.z == 0, "Only full-region updates supported (dstPos must be 0).");

    uint3 mipSize = calcMipSize(dstMipLevel);
    ASSERT(extent.x == mipSize.x && extent.y == mipSize.y && extent.z == 1, "For now, only full 2D mip updates supported.");

    // 업로드 버퍼에 쓰기
    D3D12_RESOURCE_DESC desc = m_pTexResource->GetDesc();

    UINT numSubresources = desc.MipLevels * desc.DepthOrArraySize;
    std::vector<D3D12_PLACED_SUBRESOURCE_FOOTPRINT> footprints(numSubresources);
    std::vector<UINT> numRows(numSubresources);
    std::vector<UINT64> rowSizesInBytes(numSubresources);
    UINT64 totalBytes = 0;

    m_pDevice->GetCopyableFootprints(
        &desc,
        0,
        numSubresources,
        0,
        footprints.data(),
        numRows.data(),
        rowSizesInBytes.data(),
        &totalBytes);

    const UINT subresourceIndex = dstMipLevel + desc.MipLevels * dstArraySlice;
    const auto& fp = footprints[subresourceIndex];

    uint8_t* mapped = nullptr;
    CD3DX12_RANGE range(0, 0); // write-only
    HRESULT hr = m_pUploadBuffer->Map(0, &range, reinterpret_cast<void**>(&mapped));
    ASSERT(SUCCEEDED(hr), "Failed to Map upload buffer.");

    uint8_t* dstBase = mapped + fp.Offset;
    const uint8_t* srcBytes = reinterpret_cast<const uint8_t*>(src);

    const UINT rowCount = numRows[subresourceIndex];
    ASSERT(rowCount == mipSize.y, "Expected rowCount == mipSize.y.");

    for (UINT row = 0; row < rowCount; ++row)
    {
        std::memcpy(
            dstBase + static_cast<size_t>(row) * fp.Footprint.RowPitch,
            srcBytes + static_cast<size_t>(row) * srcRowPitchBytes,
            srcRowPitchBytes);
    }

    m_pUploadBuffer->Unmap(0, nullptr);

    // CPU-side 버퍼도 있으면 같이 갱신
    if (m_Params.bReadable)
    {
        for (uint y = 0; y < extent.y; ++y)
        {
            for (uint x = 0; x < extent.x; ++x)
            {
                const uint8_t* p = srcBytes + static_cast<size_t>(y) * srcRowPitchBytes + x * sizeof(Color);
                Color c = *reinterpret_cast<const Color*>(p);
                cpuPixelRef({ x, y, 0 }, dstMipLevel, dstArraySlice) = c;
            }
        }
    }

    m_bDirtyGPU = true;
}

void D3D12Texture::CopyRegion(const ITexture& src)
{
    // 현재는 같은 타입(D3D12Texture)끼리, 전체 복사만 지원
    const D3D12Texture* pSrcTex = dynamic_cast<const D3D12Texture*>(&src);
    ASSERT(pSrcTex != nullptr, "CopyRegion: only D3D12Texture -> D3D12Texture supported.");

    ASSERT(m_pTexResource != nullptr && pSrcTex->m_pTexResource != nullptr, "Texture resources must be valid.");
    ASSERT(m_pResourceManager != nullptr, "m_pResourceManager must be valid.");

    // ResourceManager의 UpdateTextureForWrite를 이용해서 full copy
    m_pResourceManager->UpdateTextureForWrite(m_pTexResource, pSrcTex->m_pTexResource);
}

void D3D12Texture::CopyRegion(
    const ITexture& src,
    uint3 /*srcPos*/,
    uint3 /*dstPos*/,
    uint3 /*extent*/,
    uint srcMipLevel,
    uint dstMipLevel,
    uint srcArraySlice,
    uint dstArraySlice)
{
    // 단순 구현: 현재는 src/dst 전체 subresource 복사만 지원
    const D3D12Texture* pSrcTex = dynamic_cast<const D3D12Texture*>(&src);
    ASSERT(pSrcTex != nullptr, "CopyRegion: only D3D12Texture -> D3D12Texture supported.");

    ASSERT(m_pTexResource != nullptr && pSrcTex->m_pTexResource != nullptr, "Texture resources must be valid.");
    ASSERT(m_pResourceManager != nullptr, "m_pResourceManager must be valid.");

    // 이 경우에는 ResourceManager에 "subresource copy" 기능이 없으니,
    // TODO: 필요하면 별도 커맨드 리스트 만들어서 CopyTextureRegion 구현.
    // 일단은 full UpdateTextureForWrite로 처리.
    UNREFERENCED_PARAMETER(srcMipLevel);
    UNREFERENCED_PARAMETER(dstMipLevel);
    UNREFERENCED_PARAMETER(srcArraySlice);
    UNREFERENCED_PARAMETER(dstArraySlice);

    m_pResourceManager->UpdateTextureForWrite(m_pTexResource, pSrcTex->m_pTexResource);
}

// -----------------------------
// CPU-side GetPixel / SetPixel
// -----------------------------

Color D3D12Texture::GetPixel(const uint3& pos, uint mipLevel, uint arraySlice) const
{
    if (!m_Params.bReadable)
    {
        // 읽기 허용 안 된 텍스처
        return Color(0, 0, 0, 0);
    }
    return cpuPixelRef(pos, mipLevel, arraySlice);
}

Color D3D12Texture::GetPixelBilinear(const FVector2& uv, uint mipLevel, uint arraySlice) const
{
    if (!m_Params.bReadable)
    {
        return Color(0, 0, 0, 0);
    }

    uint3 sz = calcMipSize(mipLevel);
    float u = std::fmod(std::fmod(uv.x, 1.0f) + 1.0f, 1.0f);
    float v = std::fmod(std::fmod(uv.y, 1.0f) + 1.0f, 1.0f);

    float fx = u * (sz.x - 1);
    float fy = v * (sz.y - 1);

    uint x0 = static_cast<uint>(fx);
    uint y0 = static_cast<uint>(fy);
    uint x1 = std::min(x0 + 1, sz.x - 1);
    uint y1 = std::min(y0 + 1, sz.y - 1);

    float tx = fx - x0;
    float ty = fy - y0;

    Color c00 = cpuPixelRef({ x0, y0, 0 }, mipLevel, arraySlice);
    Color c10 = cpuPixelRef({ x1, y0, 0 }, mipLevel, arraySlice);
    Color c01 = cpuPixelRef({ x0, y1, 0 }, mipLevel, arraySlice);
    Color c11 = cpuPixelRef({ x1, y1, 0 }, mipLevel, arraySlice);

    Color cx0 = c00 * (1.0f - tx) + c10 * tx;
    Color cx1 = c01 * (1.0f - tx) + c11 * tx;
    return cx0 * (1.0f - ty) + cx1 * ty;
}

void D3D12Texture::SetPixel(const uint3& pos, const Color& color, uint mipLevel, uint arraySlice)
{
    if (!m_Params.bReadable)
        return;

    cpuPixelRef(pos, mipLevel, arraySlice) = color;

    // CPU-side 버퍼에서 업로드 버퍼로 복사하려면 추가 코드 필요.
    // 지금은 "raw CopyRegion()" 경로로만 업로드한다고 가정.
}

bool D3D12Texture::InitializeFromExistingResource(const TEXTURE_DESC& desc, ID3D12Resource* pExistingResource)
{
    ASSERT(!m_bInitialized, "D3D12Texture already initialized.");
    ASSERT(m_pDevice != nullptr, "D3D12Texture: m_pDevice is null.");
    ASSERT(m_pSrvAllocator != nullptr, "D3D12Texture: m_pSrvAllocator is null.");
    ASSERT(pExistingResource != nullptr, "InitializeFromExistingResource: pExistingResource is null.");

    m_Params = desc;
    m_pTexResource = pExistingResource;

    // 동적 텍스처로 쓰고 싶다면 desc.bDynamic = true 로 넘기면 업로드 버퍼 만들어줌
    if (m_Params.bDynamic)
    {
        createUploadBufferForDynamic();
    }

    createSRV();

    if (m_Params.bReadable)
    {
        size_t totalPixels = 0;
        for (uint mip = 0; mip < m_Params.mipCount; ++mip)
        {
            uint3 sz = calcMipSize(mip);
            totalPixels += static_cast<size_t>(sz.x) * sz.y * sz.z * m_Params.arraySize;
        }
        m_SystemMemory.resize(totalPixels);
    }

    m_bInitialized = true;
    m_bDirtyGPU = false;
    m_bSamplerDirty = false;
    return true;
}
