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
				// Negative: 우측-상단 기준으로 2x2 블록을 잡도록 오른쪽 anchor
				//           (u0,u1) = (W-2 - 2x, W-1 - 2x)
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
	ASSERT(depthAtlasImage.GetFormat() == TEXFORMAT_R16_UNORM, "Casper depth atlas must be R16_UNORM.");
	const uint W0 = depthAtlasImage.GetWidth();
	const uint H0 = depthAtlasImage.GetHeight();
	uint arraySize = std::max(1u, depthAtlasImage.GetArraySize());
	ASSERT(arraySize == 6, "Depth atlas must have 6 faces for a cubemap.");

	// --- Build min-mip chain on CPU for each face (tight-packed: rowPitch = width*2) ---

	const uint mipCount = CalcFullMipCount(W0, H0);
	const size_t texelsPerFace = SumTexelsOverMips(W0, H0);
	std::vector<uint16_t> tightPacked(texelsPerFace * arraySize, 0xFFFF); // << 기본 1.0로 전체 초기화

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
			const uint8_t* srow = (const uint8_t*)srcTop + y * srcRowPitch;
			uint16_t* drow = dstTop + y * W0;
			std::memcpy(drow, srow, size_t(W0) * sizeof(uint16_t));
		}

		faceBase += texelsPerFace;
	}

	// 레벨 포인터 헬퍼
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

	// --- 순수 2×2 min 체인 빌드 ---
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

		// 하위 레벨 버퍼는 이미 0xFFFF로 채워져 있음(위에서 전체 초기화)
		BuildMinMip_R16_UNORM_Cube_Anchored(prevFaces, outFaces, curW, curH);

		curW = std::max(1u, curW >> 1);
		curH = std::max(1u, curH >> 1);
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
