#include "pch.h"
#include "SingleDescriptorAllocator.h"
#include "D3D12Renderer.h"
#include "D3D12ResourceManager.h"
#include "TextureManager.h"

bool TextureManager::Initialize(D3D12Renderer* pRenderer, int numExpectedItems)
{
	m_pRenderer = pRenderer;
	m_pResourceManager = pRenderer->GetResourceManager();

	m_HashTable.reserve(numExpectedItems);

	return true;
}

void TextureManager::Cleanup()
{
	ASSERT(m_HashTable.size() > 0, "Texture resource leak detected.\n");
	m_HashTable.clear();
}

TextureHandle* TextureManager::CreateTextureFromFile(const wchar_t* wchFileName)
{
	ID3D12Device* pD3DDevice = m_pRenderer->GetD3DDevice();
	SingleDescriptorAllocator* pSingleDescriptorAllocator = m_pRenderer->GetSingleDescriptorAllocator();
	bool bUseGpuUploadHeaps = m_pRenderer->IsGpuUploadHeapsEnabledInl();

	ID3D12Resource* pTexResource = nullptr;
	D3D12_CPU_DESCRIPTOR_HANDLE srv = {};
	D3D12_RESOURCE_DESC	desc = {};
	TextureHandle* pOutTexHandle = nullptr;

	if (auto it = m_HashTable.find(wchFileName); it != m_HashTable.end())
	{
		pOutTexHandle = it->second;
		++it->second->RefCount;
	}
	else
	{
		if (m_pResourceManager->CreateTextureFromFile(&pTexResource, &desc, wchFileName, bUseGpuUploadHeaps))
		{
			D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
			srvDesc.Format = desc.Format;
			srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;

			if (desc.Dimension == D3D12_RESOURCE_DIMENSION_TEXTURE2D)
			{
				if (desc.DepthOrArraySize > 1)
				{
					if (desc.DepthOrArraySize == 6 && (desc.Flags & D3D12_RESOURCE_FLAG_ALLOW_SIMULTANEOUS_ACCESS) == 0)
					{
						// CubeMap
						srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURECUBE;
						srvDesc.TextureCube.MipLevels = desc.MipLevels;
					}
					else
					{
						// 2D Array
						srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2DARRAY;
						srvDesc.Texture2DArray.MipLevels = desc.MipLevels;
						srvDesc.Texture2DArray.ArraySize = desc.DepthOrArraySize;
					}
				}
				else
				{
					// Normal 2D texture
					srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
					srvDesc.Texture2D.MipLevels = desc.MipLevels;
				}
			}
			else if (desc.Dimension == D3D12_RESOURCE_DIMENSION_TEXTURE3D)
			{
				srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE3D;
				srvDesc.Texture2D.MipLevels = desc.MipLevels;
			}
			else
			{
				ASSERT(false, "Unsupported texture type.\n");
			}

			// Descriptor heap allocation and SRV creation
			if (pSingleDescriptorAllocator->AllocDescriptorHandle(&srv))
			{
				pD3DDevice->CreateShaderResourceView(pTexResource, &srvDesc, srv);

				pOutTexHandle = allocTextureHandle();
				pOutTexHandle->pTexResource = pTexResource;
				pOutTexHandle->bFromFile = TRUE;
				pOutTexHandle->SRV = srv;
				pOutTexHandle->Dimension = srvDesc.ViewDimension;

				auto bResult = m_HashTable.insert({ wchFileName,  pOutTexHandle }).second;
				ASSERT(bResult, "HashTable insertion failed.\n");
			}
			else
			{
				SAFE_RELEASE(pTexResource);
			}
		}
	}

	return pOutTexHandle;
}

TextureHandle* TextureManager::CreateDynamicTexture(uint texWidth, uint texHeight)
{
	ID3D12Device* pD3DDevice = m_pRenderer->GetD3DDevice();
	SingleDescriptorAllocator* pSingleDescriptorAllocator = m_pRenderer->GetSingleDescriptorAllocator();
	TextureHandle* pTexHandle = nullptr;

	ID3D12Resource* pTexResource = nullptr;
	ID3D12Resource* pUploadBuffer = nullptr;
	D3D12_CPU_DESCRIPTOR_HANDLE srv = {};

	DXGI_FORMAT texFormat = DXGI_FORMAT_R8G8B8A8_UNORM;

	if (m_pResourceManager->CreateTexturePair(&pTexResource, &pUploadBuffer, texWidth, texHeight, texFormat))
	{
		D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
		srvDesc.Format = texFormat;
		srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
		srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
		srvDesc.Texture2D.MipLevels = 1;

		if (pSingleDescriptorAllocator->AllocDescriptorHandle(&srv))
		{
			pD3DDevice->CreateShaderResourceView(pTexResource, &srvDesc, srv);

			pTexHandle = allocTextureHandle();
			pTexHandle->pTexResource = pTexResource;
			pTexHandle->pUploadBuffer = pUploadBuffer;
			pTexHandle->SRV = srv;
		}
		else
		{
			SAFE_RELEASE(pTexResource);
			SAFE_RELEASE(pUploadBuffer);
		}
	}

	return pTexHandle;
}

TextureHandle* TextureManager::CreateImmutableTexture(uint texWidth, uint texHeight, DXGI_FORMAT format, const uint8_t* pInitImage)
{
	ID3D12Device* pD3DDevice = m_pRenderer->GetD3DDevice();
	SingleDescriptorAllocator* pSingleDescriptorAllocator = m_pRenderer->GetSingleDescriptorAllocator();
	TextureHandle* pTexHandle = nullptr;

	ID3D12Resource* pTexResource = nullptr;
	D3D12_CPU_DESCRIPTOR_HANDLE srv = {};

	if (m_pResourceManager->CreateTexture(&pTexResource, texWidth, texHeight, format, pInitImage))
	{
		D3D12_SHADER_RESOURCE_VIEW_DESC SRVDesc = {};
		SRVDesc.Format = format;
		SRVDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
		SRVDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
		SRVDesc.Texture2D.MipLevels = 1;

		if (pSingleDescriptorAllocator->AllocDescriptorHandle(&srv))
		{
			pD3DDevice->CreateShaderResourceView(pTexResource, &SRVDesc, srv);

			pTexHandle = allocTextureHandle();
			pTexHandle->pTexResource = pTexResource;
			pTexHandle->SRV = srv;
		}
		else
		{
			SAFE_RELEASE(pTexResource);
		}
	}

	return pTexHandle;
}

enum CubeFace { PX = 0, NX = 1, PY = 2, NY = 3, PZ = 4, NZ = 5 };

// u,v는 [0..W-1],[0..H-1] 정수 격자 좌표(픽셀 인덱스)라고 가정.
// 경계를 벗어나면 이웃 face 및 (u,v) 변환으로 리맵.
struct SeamSample
{
	int face;
	int u;
	int v;
};

// (DirectX 표준 큐브맵 좌표 기준) 각 face 가장자리에서의 이웃과 (u,v) 변환.
// 주의: 프로젝트에서 face 이미지의 실제 회전/플립이 다르면 이 부분만 수정.
static inline SeamSample RemapSeamPX(int u, int v, int W, int H)
{
	if (u < 0)      return { PZ,           0 + v,         H - 1, }; // PX의 u<0는 PZ의 위쪽 가장자리로
	if (u >= W)     return { NZ,           0 + (H - 1 - v),   0, };
	if (v < 0)      return { PY,           0 + u,         0, };
	/* v >= H */    return { NY,           0 + u,         H - 1, };
}

static inline SeamSample RemapSeamNX(int u, int v, int W, int H)
{
	if (u < 0)      return { NZ,           0 + v,         H - 1, };
	if (u >= W)     return { PZ,           0 + (H - 1 - v),   0, };
	if (v < 0)      return { PY,           W - 1 - u,       0, };
	/* v >= H */    return { NY,           W - 1 - u,       H - 1, };
}

static inline SeamSample RemapSeamPY(int u, int v, int W, int H)
{
	if (u < 0)      return { PZ,           0,             0 + u, };
	if (u >= W)     return { NZ,           0,             0 + (W - 1 - u), };
	if (v < 0)      return { NX,           W - 1 - u,       0, };
	/* v >= H */    return { PX,           0 + u,         0, };
}

static inline SeamSample RemapSeamNY(int u, int v, int W, int H)
{
	if (u < 0)      return { PZ,           W - 1,           0 + (W - 1 - u), };
	if (u >= W)     return { NZ,           W - 1,           0 + u, };
	if (v < 0)      return { PX,           0 + u,         H - 1, };
	/* v >= H */    return { NX,           W - 1 - u,       H - 1, };
}

static inline SeamSample RemapSeamPZ(int u, int v, int W, int H)
{
	if (u < 0)      return { NX,           0,             0 + v, };
	if (u >= W)     return { PX,           W - 1,           0 + v, };
	if (v < 0)      return { PY,           0 + u,         0, };
	/* v >= H */    return { NY,           0 + u,         H - 1, };
}

static inline SeamSample RemapSeamNZ(int u, int v, int W, int H)
{
	if (u < 0)      return { PX,           0,             0 + (H - 1 - v), };
	if (u >= W)     return { NX,           W - 1,           0 + (H - 1 - v), };
	if (v < 0)      return { PY,           W - 1 - u,       0, };
	/* v >= H */    return { NY,           W - 1 - u,       H - 1, };
}

static inline SeamSample RemapSeam(int face, int u, int v, int W, int H)
{
	switch (face)
	{
	case PX: return RemapSeamPX(u, v, W, H);
	case NX: return RemapSeamNX(u, v, W, H);
	case PY: return RemapSeamPY(u, v, W, H);
	case NY: return RemapSeamNY(u, v, W, H);
	case PZ: return RemapSeamPZ(u, v, W, H);
	case NZ: return RemapSeamNZ(u, v, W, H);
	default: return { face, std::clamp(u,0,W - 1), std::clamp(v,0,H - 1) };
	}
}

// inFaces[f], outFaces[f] : 타이트(패딩 없음), 크기 W*H
// iterations >= 1 이면 그 횟수만큼 ping-pong 에로전 수행
static void Erode3x3_R16_UNORM_Cube(
	const uint16_t* inFaces[6],
	uint16_t* outFaces[6],
	uint W, uint H,
	int iterations = 1)
{
	// seam-aware 샘플러 (경계 넘어가면 이웃 face로 리맵)
	auto SampleFaceSafe = [&](int face, int u, int v) -> uint16_t
		{
			if ((unsigned)u < W && (unsigned)v < H)
				return inFaces[face][v * W + u];

			SeamSample s = RemapSeam(face, u, v, (int)W, (int)H);
			s.u = std::clamp(s.u, 0, (int)W - 1);
			s.v = std::clamp(s.v, 0, (int)H - 1);
			return inFaces[s.face][s.v * W + s.u];
		};

	// 내부 ping-pong 버퍼 (iterations>1 대비)
	std::vector<std::vector<uint16_t>> ping(6), pong(6);
	for (int f = 0; f < 6; ++f)
	{
		ping[f].assign(W * H, 0);
		pong[f].assign(W * H, 0);
		// 최초 입력 복사
		std::memcpy(ping[f].data(), inFaces[f], size_t(W) * H * sizeof(uint16_t));
	}

	auto step_once = [&](std::vector<std::vector<uint16_t>>& src,
		std::vector<std::vector<uint16_t>>& dst)
		{
			for (int f = 0; f < 6; ++f)
			{
				const uint16_t* s = src[f].data();
				uint16_t* d = dst[f].data();
				for (uint y = 0; y < H; ++y)
				{
					for (uint x = 0; x < W; ++x)
					{
						// 3x3 min (seam-aware)
						uint16_t m = 0xFFFF;
						for (int dy = -1; dy <= 1; ++dy)
						{
							for (int dx = -1; dx <= 1; ++dx)
							{
								int ux = int(x) + dx;
								int vy = int(y) + dy;
								uint16_t v = SampleFaceSafe(f, ux, vy);
								m = (v < m) ? v : m;
							}
						}
						d[y * W + x] = m;
					}
				}
			}
		};

	// iterations회 에로전
	for (int it = 0; it < iterations; ++it)
	{
		step_once(ping, pong);
		// swap
		for (int f = 0; f < 6; ++f)
			ping[f].swap(pong[f]);
	}

	// 결과를 outFaces로 복사
	for (int f = 0; f < 6; ++f)
		std::memcpy(outFaces[f], ping[f].data(), size_t(W) * H * sizeof(uint16_t));
}


// prevFaces[f]  : 이전 레벨(상위 해상도) face f의 시작 포인터 (size: W*H)
// outFaces[f]   : 이번 레벨(하위 해상도) face f의 시작 포인터 (size: (W/2)*(H/2))
// 모든 버퍼는 "타이트"하게 행연속(패딩 없음)이라고 가정.
static void BuildMinMip_R16_UNORM_Cube(
	const uint16_t* prevFaces[6],
	uint16_t* outFaces[6],
	uint W, uint H)
{
	const uint Wo = std::max(1u, W / 2);
	const uint Ho = std::max(1u, H / 2);

	auto SampleFaceSafe = [&](int face, int u, int v) -> uint16_t
		{
			if ((unsigned)u < W && (unsigned)v < H)
				return prevFaces[face][v * W + u];

			SeamSample s = RemapSeam(face, u, v, (int)W, (int)H);
			s.u = std::clamp(s.u, 0, (int)W - 1);
			s.v = std::clamp(s.v, 0, (int)H - 1);
			return prevFaces[s.face][s.v * W + s.u];
		};

	for (int f = 0; f < 6; ++f)
	{
		uint16_t* dst = outFaces[f];
		for (uint y = 0; y < Ho; ++y)
		{
			const int v0 = int(2 * y + 0);
			const int v1 = int(2 * y + 1);
			for (uint x = 0; x < Wo; ++x)
			{
				const int u0 = int(2 * x + 0);
				const int u1 = int(2 * x + 1);

				uint16_t a = SampleFaceSafe(f, u0, v0);
				uint16_t b = SampleFaceSafe(f, u1, v0);
				uint16_t c = SampleFaceSafe(f, u0, v1);
				uint16_t d = SampleFaceSafe(f, u1, v1);
				dst[y * Wo + x] = std::min(std::min(a, b), std::min(c, d));
			}
		}
	}
}


static inline uint CalcFullMipCount(uint w, uint h)
{
	uint m = 1;
	while (w > 1 || h > 1) { w = std::max(1u, w >> 1); h = std::max(1u, h >> 1); ++m; }
	return m;
}

static inline size_t SumTexelsOverMips(uint w, uint h)
{
	size_t sum = 0;
	while (true) {
		sum += size_t(w) * size_t(h);
		if (w == 1 && h == 1) break;
		w = std::max(1u, w >> 1);
		h = std::max(1u, h >> 1);
	}
	return sum;
}

TextureHandle* TextureManager::CreateCasperDepthAtlasTextureFromFile(const wchar_t* wchFileName)
{
	ID3D12Device* pD3DDevice = m_pRenderer->GetD3DDevice();
	SingleDescriptorAllocator* pSingleDescriptorAllocator = m_pRenderer->GetSingleDescriptorAllocator();

	ID3D12Resource* pTexResource = nullptr;
	D3D12_CPU_DESCRIPTOR_HANDLE srv = {};
	TextureHandle* pOutTexHandle = nullptr;

	// --- Load input image (expects R16_UNORM, cube as arraySize=6) ---
	Image depthAtlasImage;
	bool ok = depthAtlasImage.Load(wchFileName);
	ASSERT(ok && depthAtlasImage.IsValid(), "Failed to load depth atlas image.");
	ASSERT(depthAtlasImage.GetFormat() == Image::FORMAT_R16_UNORM, "Casper depth atlas must be R16_UNORM.");
	const uint W0 = depthAtlasImage.GetWidth();
	const uint H0 = depthAtlasImage.GetHeight();
	uint arraySize = std::max(1u, depthAtlasImage.GetArraySize());
	ASSERT(arraySize == 6, "Depth atlas must have 6 faces for a cubemap.");

	// --- Build min-mip chain on CPU for each face (tight-packed: rowPitch = width*2) ---
	const uint mipCount = CalcFullMipCount(W0, H0);

	// Allocate tight-packed buffer for all faces & mips: [face0 m0..mN][face1 m0..mN]...
	const size_t texelsPerFace = SumTexelsOverMips(W0, H0);
	std::vector<uint16_t> tightPacked;
	tightPacked.resize(texelsPerFace * arraySize);

	size_t faceBase = 0;
	for (int f = 0; f < 6; ++f)
	{
		const void* srcTop = depthAtlasImage.GetDataPtr(f, 0, 0);
		const size_t srcRowPitch = depthAtlasImage.GetRowPitch(f, 0, 0);
		uint16_t* dstTop = tightPacked.data() + faceBase;

		for (uint y = 0; y < H0; ++y)
		{
			const uint8_t* srow = (const uint8_t*)srcTop + y * srcRowPitch;
			uint16_t* drow = dstTop + y * W0;
			std::memcpy(drow, srow, size_t(W0) * sizeof(uint16_t));
		}
		faceBase += texelsPerFace;
	}

	// 레벨 포인터
	auto FaceLevelPtr = [&](int face, uint level) -> uint16_t*
		{
			size_t base = size_t(face) * texelsPerFace;
			uint w = W0, h = H0;
			size_t off = 0;
			for (uint m = 0; m < level; ++m) {
				off += size_t(w) * size_t(h);
				w = std::max(1u, w >> 1);
				h = std::max(1u, h >> 1);
			}
			return tightPacked.data() + base + off;
		};

	uint curW = W0, curH = H0;
	for (uint level = 1; level < mipCount; ++level)
	{
		const uint nextW = std::max(1u, curW >> 1);
		const uint nextH = std::max(1u, curH >> 1);

		// prev(level-1), eroded(level-1), out(level) 포인터
		const uint16_t* prevFaces[6] = {
			FaceLevelPtr(PX, level - 1),
			FaceLevelPtr(NX, level - 1),
			FaceLevelPtr(PY, level - 1),
			FaceLevelPtr(NY, level - 1),
			FaceLevelPtr(PZ, level - 1),
			FaceLevelPtr(NZ, level - 1),
		};

		// 에로전 결과 임시 버퍼 (타이트)
		std::vector<std::vector<uint16_t>> eroded(6);
		for (int f = 0; f < 6; ++f) eroded[f].assign(curW * curH, 0);

		const uint16_t* erodedIn[6] = { nullptr,nullptr,nullptr,nullptr,nullptr,nullptr };
		uint16_t* erodedOut[6] = { nullptr,nullptr,nullptr,nullptr,nullptr,nullptr };
		for (int f = 0; f < 6; ++f) { erodedOut[f] = eroded[f].data(); }

		// prev → eroded (3×3 min, 1회; 보수성 강화)
		Erode3x3_R16_UNORM_Cube(prevFaces, erodedOut, curW, curH, /*iterations=*/1);

		// 다운샘플 입력 포인터 배열로 넘김
		for (int f = 0; f < 6; ++f) erodedIn[f] = eroded[f].data();

		uint16_t* outFaces[6] = {
			FaceLevelPtr(PX, level),
			FaceLevelPtr(NX, level),
			FaceLevelPtr(PY, level),
			FaceLevelPtr(NY, level),
			FaceLevelPtr(PZ, level),
			FaceLevelPtr(NZ, level),
		};

		// (에로전된) prev level → next level (2×2 min)
		BuildMinMip_R16_UNORM_Cube(erodedIn, outFaces, curW, curH);

		curW = nextW; curH = nextH;
	}


	// --- Create GPU texture from tight-packed buffer with mips+array ---
	if (m_pResourceManager->CreateTexture(
		&pTexResource,
		W0, H0,
		DXGI_FORMAT_R16_UNORM,
		reinterpret_cast<const uint8_t*>(tightPacked.data()),
		arraySize,
		mipCount))
	{
		// Create SRV as cubemap
		D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
		srvDesc.Format = DXGI_FORMAT_R16_UNORM;
		srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
		srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURECUBE;
		srvDesc.TextureCube.MostDetailedMip = 0;
		srvDesc.TextureCube.MipLevels = mipCount;
		srvDesc.TextureCube.ResourceMinLODClamp = 0.0f;

		if (pSingleDescriptorAllocator->AllocDescriptorHandle(&srv))
		{
			pD3DDevice->CreateShaderResourceView(pTexResource, &srvDesc, srv);

			pOutTexHandle = allocTextureHandle();
			pOutTexHandle->pTexResource = pTexResource;
			pOutTexHandle->bFromFile = TRUE;
			pOutTexHandle->SRV = srv;
			pOutTexHandle->Dimension = srvDesc.ViewDimension;
		}
		else
		{
			SAFE_RELEASE(pTexResource);
		}
	}

	return pOutTexHandle;
}


void TextureManager::DeleteTexture(TextureHandle* pTexHandle)
{
	freeTextureHandle(pTexHandle);
}

TextureHandle* TextureManager::allocTextureHandle()
{
	TextureHandle* pTexHandle = new TextureHandle;
	memset(pTexHandle, 0, sizeof(TextureHandle));
	pTexHandle->RefCount = 1;
	return pTexHandle;
}

uint TextureManager::freeTextureHandle(TextureHandle* pTexHandle)
{
	ID3D12Device* pD3DDevice = m_pRenderer->GetD3DDevice();
	SingleDescriptorAllocator* pSingleDescriptorAllocator = m_pRenderer->GetSingleDescriptorAllocator();

	ASSERT(pTexHandle->RefCount > 0, "Texture handle reference count is already zero.\n");

	int refCount = --pTexHandle->RefCount;
	if (!refCount)
	{
		SAFE_RELEASE(pTexHandle->pTexResource);
		SAFE_RELEASE(pTexHandle->pUploadBuffer);
		if (pTexHandle->SRV.ptr)
		{
			pSingleDescriptorAllocator->FreeDescriptorHandle(pTexHandle->SRV);
			pTexHandle->SRV = {};
		}
		SAFE_DELETE(pTexHandle);
	}
	return refCount;
}
