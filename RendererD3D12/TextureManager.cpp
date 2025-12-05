#include "pch.h"
#include "TextureManager.h"

#include "D3D12Renderer.h"
#include "D3D12Texture.h"
#include "D3D12ResourceManager.h"
#include "SingleDescriptorAllocator.h"
#include "Generic/Image.h"   // 깊이 아틀라스용 Image

// -----------------------------
// 내부 helper들 (CASPER용)
// -----------------------------

enum CubeFace { PX = 0, NX = 1, PY = 2, NY = 3, PZ = 4, NZ = 5 };

static inline bool IsNegFace(int f) { return f == NX || f == NY || f == NZ; }

// 한 단계(레벨 N -> 레벨 N+1) 다운샘플: 2x2 min
// prevFaces: [6] = 레벨 N 각 face 버퍼 (rowPitch = W * sizeof(uint16_t))
// outFaces : [6] = 레벨 N+1 각 face 버퍼 (rowPitch = Wo * sizeof(uint16_t))
// W,H      : 레벨 N 크기
static void BuildMinMip_R16_UNORM_Cube_Anchored(
	const uint16_t* prevFaces[6],
	uint16_t* outFaces[6],
	uint32_t W, uint32_t H)
{
	const uint32_t Wo = std::max(1u, W / 2);
	const uint32_t Ho = std::max(1u, H / 2);

	auto SampleClamp = [&](const uint16_t* img, int u, int v) -> uint16_t {
		// 경계는 클램프 (파워-오브-투가 아니어도 안정)
		if (u < 0) u = 0; else if (u >= (int)W) u = (int)W - 1;
		if (v < 0) v = 0; else if (v >= (int)H) v = (int)H - 1;
		return img[v * W + u];
		};

	for (int f = 0; f < 6; ++f)
	{
		const bool neg = IsNegFace(f);
		const uint16_t* src = prevFaces[f];
		uint16_t* dst = outFaces[f];

		for (uint32_t y = 0; y < Ho; ++y)
		{
			const int v0 = int(2 * y + 0);
			const int v1 = int(2 * y + 1);

			for (uint32_t x = 0; x < Wo; ++x)
			{
				// Positive: (u0,u1) = (2x, 2x+1)
				// Negative: 우측-상단 기준 2x2 블록을 anchor
				int u0 = neg ? (int(W) - 2 - int(2 * x)) : int(2 * x);
				int u1 = u0 + 1;

				uint16_t a = SampleClamp(src, u0, v0);
				uint16_t b = SampleClamp(src, u1, v0);
				uint16_t c = SampleClamp(src, u0, v1);
				uint16_t d = SampleClamp(src, u1, v1);

				uint32_t wx = neg ? (Wo - 1 - x) : x;
				dst[y * Wo + wx] = std::min(std::min(a, b), std::min(c, d));
			}
		}
	}
}

static inline uint CalcFullMipCount(uint w, uint h)
{
	uint m = 1;
	while (w > 1 || h > 1)
	{
		w = std::max(1u, w >> 1);
		h = std::max(1u, h >> 1);
		++m;
	}
	return m;
}

static inline size_t SumTexelsOverMips(uint w, uint h)
{
	size_t sum = 0;
	while (true)
	{
		sum += size_t(w) * size_t(h);
		if (w == 1 && h == 1) break;
		w = std::max(1u, w >> 1);
		h = std::max(1u, h >> 1);
	}
	return sum;
}

// -----------------------------
// TextureManager
// -----------------------------

TextureManager::~TextureManager()
{
	Cleanup();
}

bool TextureManager::Initialize(D3D12Renderer* pRenderer, int numExpectedItems)
{
	m_pRenderer = pRenderer;
	m_pResourceManager = pRenderer->GetResourceManager();
	m_pDevice = pRenderer->GetD3DDevice();              // ID3D12Device5* 라고 가정
	m_pSrvAllocator = pRenderer->GetSingleDescriptorAllocator();

	m_TextureTable.clear();
	if (numExpectedItems > 0)
		m_TextureTable.reserve(numExpectedItems);

	return true;
}

void TextureManager::Cleanup()
{
	clearAllRecords();

	m_pRenderer = nullptr;
	m_pResourceManager = nullptr;
	m_pDevice = nullptr;
	m_pSrvAllocator = nullptr;
}

// -----------------------------
// 내부 헬퍼
// -----------------------------

D3D12Texture* TextureManager::createTextureObject() const
{
	ASSERT(m_pDevice && m_pResourceManager && m_pSrvAllocator,
		"TextureManager not initialized (device/resourceManager/srvAllocator null).");
	return new D3D12Texture(m_pDevice, m_pResourceManager, m_pSrvAllocator);
}

D3D12Texture* TextureManager::findCachedTexture(const std::wstring& key)
{
	auto it = m_TextureTable.find(key);
	if (it == m_TextureTable.end())
		return nullptr;

	TextureRecord& rec = it->second;
	ASSERT(rec.pTexture != nullptr, "TextureRecord with null texture.");
	++rec.refCount;
	return rec.pTexture;
}

D3D12Texture* TextureManager::cacheTexture(const std::wstring& key, D3D12Texture* pTex)
{
	ASSERT(pTex != nullptr, "cacheTexture: pTex is null.");

	TextureRecord rec;
	rec.pTexture = pTex;
	rec.refCount = 1;

	auto [it, inserted] = m_TextureTable.emplace(key, rec);
	if (!inserted)
	{
		std::cout << "TextureManager::cacheTexture: key already existed. Reusing existing texture." << std::endl;
		delete pTex;
		TextureRecord& existing = it->second;
		++existing.refCount;
		return existing.pTexture;
	}
	return pTex;
}

void TextureManager::releaseTexture(D3D12Texture* pTex)
{
	if (!pTex)
		return;

	// 1) 캐시에서 찾는다.
	for (auto it = m_TextureTable.begin(); it != m_TextureTable.end(); ++it)
	{
		TextureRecord& rec = it->second;
		if (rec.pTexture == pTex)
		{
			ASSERT(rec.refCount > 0, "TextureManager: negative refcount.");
			--rec.refCount;
			if (rec.refCount == 0)
			{
				delete rec.pTexture;
				m_TextureTable.erase(it);
			}
			return;
		}
	}

	// 2) 캐시에 없는 경우 (동적/임뮤터블 등) → 바로 delete
	delete pTex;
}

void TextureManager::clearAllRecords()
{
	for (auto& kv : m_TextureTable)
	{
		TextureRecord& rec = kv.second;
		if (rec.pTexture)
		{
			ASSERT(rec.refCount > 0, "TextureManager::clearAllRecords: non-zero refcount on cleanup.");
			delete rec.pTexture;
		}
	}
	m_TextureTable.clear();
}

// -----------------------------
// 공용 API
// -----------------------------

void TextureManager::DeleteTexture(D3D12Texture* pTexture)
{
	releaseTexture(pTexture);
}

// 파일 기반 텍스처 (캐시 + refcount)
D3D12Texture* TextureManager::CreateTextureFromFile(const wchar_t* wchFileName)
{
	ASSERT(m_pRenderer && m_pResourceManager && m_pDevice && m_pSrvAllocator,
		"TextureManager not initialized.");

	if (!wchFileName || !wchFileName[0])
		return nullptr;

	std::wstring key(wchFileName);

	// 1) 캐시 체크
	if (D3D12Texture* cached = findCachedTexture(key))
		return cached;

	// 2) 새로 로드
	ID3D12Resource* pTexResource = nullptr;
	D3D12_RESOURCE_DESC desc = {};
	bool bUseGpuUploadHeaps = m_pRenderer->IsGpuUploadHeapsEnabledInl();

	if (!m_pResourceManager->CreateTextureFromFile(&pTexResource, &desc, wchFileName, bUseGpuUploadHeaps))
	{
		return nullptr;
	}

	// 3) TEXTURE_DESC 채우기
	TEXTURE_DESC texDesc{};
	texDesc.size = {
		static_cast<uint>(desc.Width),
		static_cast<uint>(desc.Height),
		(desc.Dimension == D3D12_RESOURCE_DIMENSION_TEXTURE3D)
			? static_cast<uint>(desc.DepthOrArraySize)
			: 1u
	};

	texDesc.mipCount = desc.MipLevels;
	texDesc.arraySize = (desc.Dimension == D3D12_RESOURCE_DIMENSION_TEXTURE3D)
		? 1u
		: static_cast<uint>(desc.DepthOrArraySize);
	texDesc.sampleCount = desc.SampleDesc.Count;
	texDesc.format = static_cast<TEXFORMAT>(desc.Format);  // DXGI 값과 맞춰둔 enum

	texDesc.bDynamic = false; // 파일 텍스처는 기본 immutable
	texDesc.bReadable = false; // 필요 시 true
	texDesc.bDataSRGB = false; // TODO: sRGB면 true로 세팅
	texDesc.filterMode = ITexture::FilterMode::Bilinear;
	texDesc.wrapU = ITexture::WrapMode::Repeat;
	texDesc.wrapV = ITexture::WrapMode::Repeat;
	texDesc.wrapW = ITexture::WrapMode::Repeat;
	texDesc.anisoLevel = 1;
	texDesc.mipMapBias = 0.0f;

	// Dimension 판정
	if (desc.Dimension == D3D12_RESOURCE_DIMENSION_TEXTURE3D)
	{
		texDesc.dimension = ITexture::Dimension::Tex3D;
	}
	else
	{
		if (desc.DepthOrArraySize > 1)
		{
			// 기존 코드와 동일하게 cube/array 분기
			if (desc.DepthOrArraySize == 6 &&
				(desc.Flags & D3D12_RESOURCE_FLAG_ALLOW_SIMULTANEOUS_ACCESS) == 0)
			{
				texDesc.dimension = ITexture::Dimension::Cube;
			}
			else
			{
				texDesc.dimension = ITexture::Dimension::Tex2DArray;
			}
		}
		else
		{
			texDesc.dimension = ITexture::Dimension::Tex2D;
		}
	}

	// 4) D3D12Texture 래핑
	D3D12Texture* pTex = createTextureObject();
	if (!pTex->InitializeFromExistingResource(texDesc, pTexResource))
	{
		SAFE_RELEASE(pTexResource);
		delete pTex;
		return nullptr;
	}

	// 5) 캐시에 넣고 반환
	return cacheTexture(key, pTex);
}

// 동적 텍스처 (비캐시)
D3D12Texture* TextureManager::CreateDynamicTexture(uint texWidth, uint texHeight, DXGI_FORMAT format)
{
	D3D12Texture* pTex = createTextureObject();

	TEXTURE_DESC desc{};
	desc.dimension = ITexture::Dimension::Tex2D;
	desc.size = { texWidth, texHeight, 1u };
	desc.mipCount = 1;
	desc.arraySize = 1;
	desc.sampleCount = 1;
	desc.format = static_cast<TEXFORMAT>(format);
	desc.bDynamic = true;   // 업로드 버퍼 생성
	desc.bReadable = false;  // 필요시 true
	desc.bDataSRGB = false;
	desc.filterMode = ITexture::FilterMode::Bilinear;
	desc.wrapU = ITexture::WrapMode::Clamp;
	desc.wrapV = ITexture::WrapMode::Clamp;
	desc.wrapW = ITexture::WrapMode::Clamp;
	desc.anisoLevel = 1;
	desc.mipMapBias = 0.0f;

	if (!pTex->Initialize(desc))
	{
		delete pTex;
		return nullptr;
	}

	// 동적 텍스처는 캐시 안함
	return pTex;
}

// Immutable 텍스처 (비캐시, 초기 데이터 전달)
D3D12Texture* TextureManager::CreateImmutableTexture(
	uint texWidth,
	uint texHeight,
	DXGI_FORMAT format,
	const uint8_t* pInitImage)
{
	ID3D12Resource* pTexResource = nullptr;

	if (!m_pResourceManager->CreateTexture(&pTexResource, texWidth, texHeight, format, pInitImage))
	{
		return nullptr;
	}

	TEXTURE_DESC desc{};
	desc.dimension = ITexture::Dimension::Tex2D;
	desc.size = { texWidth, texHeight, 1u };
	desc.mipCount = 1;
	desc.arraySize = 1;
	desc.sampleCount = 1;
	desc.format = static_cast<TEXFORMAT>(format);
	desc.bDynamic = false;
	desc.bReadable = false;
	desc.bDataSRGB = false;
	desc.filterMode = ITexture::FilterMode::Bilinear;
	desc.wrapU = ITexture::WrapMode::Clamp;
	desc.wrapV = ITexture::WrapMode::Clamp;
	desc.wrapW = ITexture::WrapMode::Clamp;
	desc.anisoLevel = 1;
	desc.mipMapBias = 0.0f;

	D3D12Texture* pTex = createTextureObject();
	if (!pTex->InitializeFromExistingResource(desc, pTexResource))
	{
		SAFE_RELEASE(pTexResource);
		delete pTex;
		return nullptr;
	}

	// 비캐시
	return pTex;
}

// CASPER depth atlas (파일 기반, cube R16_UNORM, 캐시 + refcount)
D3D12Texture* TextureManager::CreateCasperDepthAtlasTextureFromFile(const wchar_t* wchFileName)
{
	ASSERT(m_pRenderer && m_pResourceManager && m_pDevice && m_pSrvAllocator,
		"TextureManager not initialized.");

	if (!wchFileName || !wchFileName[0])
		return nullptr;

	std::wstring key(wchFileName);

	// 1) 캐시 체크
	if (D3D12Texture* cached = findCachedTexture(key))
		return cached;

	// 2) 이미지 로드
	Image depthAtlasImage;
	bool ok = depthAtlasImage.Load(wchFileName);
	ASSERT(ok && depthAtlasImage.IsValid(), "Failed to load depth atlas image.");

	ASSERT(depthAtlasImage.GetFormat() == TEXFORMAT_R16_UNORM, "Casper depth atlas must be R16_UNORM.");

	const uint W0 = depthAtlasImage.GetWidth();
	const uint H0 = depthAtlasImage.GetHeight();
	uint arraySize = std::max(1u, depthAtlasImage.GetArraySize());
	ASSERT(arraySize == 6, "Depth atlas must have 6 faces for a cubemap.");

	// 3) CPU에서 min mip chain 생성
	const uint mipCount = CalcFullMipCount(W0, H0);
	const size_t texelsPerFace = SumTexelsOverMips(W0, H0);

	std::vector<uint16_t> tightPacked(texelsPerFace * arraySize, 0xFFFF);

	// face별 최상위 레벨 복사
	size_t faceBase = 0;
	for (int f = 0; f < 6; ++f)
	{
		const void* srcTop = depthAtlasImage.GetDataPtr(f, 0, 0);
		const size_t srcRowPitch = depthAtlasImage.GetRowPitch(f, 0, 0);
		uint16_t* dstTop = tightPacked.data() + faceBase;

		std::memset(dstTop, 0xFFFF, size_t(W0) * size_t(H0) * sizeof(uint16_t));

		for (uint y = 0; y < H0; ++y)
		{
			const uint8_t* srow = reinterpret_cast<const uint8_t*>(srcTop) + y * srcRowPitch;
			uint16_t* drow = dstTop + y * W0;
			std::memcpy(drow, srow, size_t(W0) * sizeof(uint16_t));
		}

		faceBase += texelsPerFace;
	}

	auto FaceLevelPtr = [&](int face, uint level) -> uint16_t*
		{
			size_t base = size_t(face) * texelsPerFace;
			uint w = W0, h = H0;
			size_t off = 0;
			for (uint m = 0; m < level; ++m)
			{
				off += size_t(w) * size_t(h);
				w = std::max(1u, w >> 1);
				h = std::max(1u, h >> 1);
			}
			return tightPacked.data() + base + off;
		};

	uint curW = W0, curH = H0;
	for (uint level = 1; level < mipCount; ++level)
	{
		const uint16_t* prevFaces[6] = {
			FaceLevelPtr(PX, level - 1),
			FaceLevelPtr(NX, level - 1),
			FaceLevelPtr(PY, level - 1),
			FaceLevelPtr(NY, level - 1),
			FaceLevelPtr(PZ, level - 1),
			FaceLevelPtr(NZ, level - 1),
		};

		uint16_t* outFaces[6] = {
			FaceLevelPtr(PX, level),
			FaceLevelPtr(NX, level),
			FaceLevelPtr(PY, level),
			FaceLevelPtr(NY, level),
			FaceLevelPtr(PZ, level),
			FaceLevelPtr(NZ, level),
		};

		BuildMinMip_R16_UNORM_Cube_Anchored(prevFaces, outFaces, curW, curH);

		curW = std::max(1u, curW >> 1);
		curH = std::max(1u, curH >> 1);
	}

	// 4) GPU 텍스처 생성 (arraySize=6, mipCount)
	ID3D12Resource* pTexResource = nullptr;
	if (!m_pResourceManager->CreateTexture(
		&pTexResource,
		W0, H0,
		DXGI_FORMAT_R16_UNORM,
		reinterpret_cast<const uint8_t*>(tightPacked.data()),
		arraySize,
		mipCount))
	{
		return nullptr;
	}

	// 5) TEXTURE_DESC 구성
	TEXTURE_DESC desc{};
	desc.dimension = ITexture::Dimension::Cube;
	desc.size = { W0, H0, 1u };
	desc.mipCount = mipCount;
	desc.arraySize = arraySize; // 6
	desc.sampleCount = 1;
	desc.format = TEXFORMAT_R16_UNORM;
	desc.bDynamic = false;
	desc.bReadable = false;
	desc.bDataSRGB = false;
	desc.filterMode = ITexture::FilterMode::Point; // depth 샘플링이니까 point
	desc.wrapU = ITexture::WrapMode::Clamp;
	desc.wrapV = ITexture::WrapMode::Clamp;
	desc.wrapW = ITexture::WrapMode::Clamp;
	desc.anisoLevel = 1;
	desc.mipMapBias = 0.0f;

	D3D12Texture* pTex = createTextureObject();
	if (!pTex->InitializeFromExistingResource(desc, pTexResource))
	{
		SAFE_RELEASE(pTexResource);
		delete pTex;
		return nullptr;
	}

	// 6) 캐시에 넣고 반환
	return cacheTexture(key, pTex);
}

