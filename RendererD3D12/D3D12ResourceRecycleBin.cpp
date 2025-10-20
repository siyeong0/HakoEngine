#include "pch.h"
#include "D3D12ResourceRecycleBin.h"

void D3D12ResourceRecycleBin::Initialize(
	ID3D12Device5* pD3DDevice,
	D3D12_HEAP_TYPE heapType,
	D3D12_RESOURCE_FLAGS resourceFlags,
	D3D12_RESOURCE_STATES initialResourceState,
	const WCHAR* wchResourceName)
{
	m_pD3DDevice = pD3DDevice;

	m_Buckets.clear();
	m_Buckets.reserve(16);
	size_t bufferMemSize = 65536ull; // 64KB
	for (int i = 0; i < 16; ++i)
	{
		D3D12ResourceBucket b{};
		b.MemorySize = bufferMemSize;
		m_Buckets.emplace_back(b);
		bufferMemSize *= 2;
	}

	m_HeapType = heapType;
	m_ResourceFlags = resourceFlags;
	m_InitialResouceState = initialResourceState;

	// Debug name
	if (wchResourceName)
	{
		wcscpy_s(m_wchResourceName, wchResourceName);
	}
	else
	{
		m_wchResourceName[0] = L'\0';
	}
}

void D3D12ResourceRecycleBin::Cleanup()
{
	// 1) Release all resources in pending list
	for (D3D12ResourceAllocDesc* pDesc : m_PendingResourceList)
	{
		if (pDesc)
		{
			pDesc->pResource->Release();
			SAFE_DELETE(pDesc);
		}
	}
	m_PendingResourceList.clear();

	// 2) Release all resources in buckets
	for (D3D12ResourceBucket& b : m_Buckets)
	{
		for (D3D12ResourceAllocDesc* pDesc : b.AvailableResourceList)
		{
			if (pDesc)
			{
				pDesc->pResource->Release();
				SAFE_DELETE(pDesc);
			}
		}
		b.AvailableResourceList.clear();
	}
}

ID3D12Resource* D3D12ResourceRecycleBin::Alloc(size_t memSize)
{
	ID3D12Resource* pResource = nullptr;
	D3D12ResourceBucket* pBucket = findBucket(memSize);
	ASSERT(pBucket);

	// 1) Reuse from bucket's free list
	if (!pBucket->AvailableResourceList.empty())
	{
		D3D12ResourceAllocDesc* pDesc = pBucket->AvailableResourceList.front();
		pBucket->AvailableResourceList.pop_front();

		pResource = pDesc->pResource;
		ASSERT(pResource);

		D3D12_RESOURCE_DESC desc = pResource->GetDesc();
		ASSERT(desc.Width >= memSize);

		SAFE_DELETE(pDesc);
		return pResource;
	}

	// 2) Not found -> Create new resource
	ASSERT(pBucket->MemorySize >= memSize);
	CD3DX12_HEAP_PROPERTIES heapProps(m_HeapType);
	CD3DX12_RESOURCE_DESC bufferDesc = CD3DX12_RESOURCE_DESC::Buffer(pBucket->MemorySize, m_ResourceFlags);

	HRESULT hr = m_pD3DDevice->CreateCommittedResource(
		&heapProps,
		D3D12_HEAP_FLAG_NONE,
		&bufferDesc,
		m_InitialResouceState,
		nullptr,
		IID_PPV_ARGS(&pResource));

	if (FAILED(hr) || !pResource)
	{
		ASSERT(false, "D3D12ResourceRecycleBin::Alloc - CreateCommittedResource failed");
		return nullptr;
	}

	// Debug name
	if (m_wchResourceName[0] != L'\0')
	{
		pResource->SetName(m_wchResourceName);
	}

	return pResource;
}

void D3D12ResourceRecycleBin::Update(uint64_t currTick)
{
	// 1) pending → (count 0) → move to bucket's freeList
	updatePendingResource(currTick);

	// 2) Release expired resources in bucket's freeList
	uint64_t elapsedTick = currTick - m_PrevFreedTick;
	if (elapsedTick > 1000) // 1 second
	{
		freeAllExpiredD3DResource(currTick);
		m_PrevFreedTick = currTick;
	}
}

void D3D12ResourceRecycleBin::Free(ID3D12Resource* pResource, int frameLifeCount)
{
	ASSERT(pResource, "D3D12ResourceRecycleBin::Free - pResource is null");

	D3D12_RESOURCE_DESC desc = pResource->GetDesc();

	auto* pDesc = new D3D12ResourceAllocDesc;
	pDesc->MemorySize = static_cast<DWORD>(desc.Width);
	pDesc->pResource = pResource;
	pDesc->FrameLifeCount = frameLifeCount;
	pDesc->RegisteredTick = 0; // pending 단계에서는 의미 없음

	m_PendingResourceList.emplace_back(pDesc);
}

D3D12ResourceBucket* D3D12ResourceRecycleBin::findBucket(size_t memSize)
{
	for (D3D12ResourceBucket& b : m_Buckets)
	{
		if (memSize <= b.MemorySize)
		{
			return &b;
		}
	}
	ASSERT(false, "No suitable bucket found. Too large size requested?");
	return nullptr;
}

uint D3D12ResourceRecycleBin::updatePendingResource(uint64_t currTick)
{
	uint numRemaining = 0;

	for (auto iter = m_PendingResourceList.begin(); iter != m_PendingResourceList.end(); )
	{
		D3D12ResourceAllocDesc* pDesc = *iter;
		if (pDesc->FrameLifeCount <= 0)
			__debugbreak();

		int iLife = --pDesc->FrameLifeCount;
		if (iLife == 0)
		{
			// pending → freeList
			D3D12ResourceBucket* pBucket = findBucket(static_cast<UINT>(pDesc->MemorySize));
			ASSERT(pBucket);

			pDesc->RegisteredTick = currTick; // Entrance time
			iter = m_PendingResourceList.erase(iter);
			pBucket->AvailableResourceList.push_back(pDesc);
		}
		else
		{
			++numRemaining;
			++iter;
		}
	}

	return numRemaining;
}

void D3D12ResourceRecycleBin::freeAllExpiredD3DResource(uint64_t currTick)
{
	const uint64_t FREE_EXPIRED_D3DRESOURCE_TICK = 1000ull * 60ull; // 1 minute

	for (D3D12ResourceBucket& b : m_Buckets)
	{
		for (auto it = b.AvailableResourceList.begin(); it != b.AvailableResourceList.end(); )
		{
			D3D12ResourceAllocDesc* pDesc = *it;
			if ((currTick - pDesc->RegisteredTick) > FREE_EXPIRED_D3DRESOURCE_TICK)
			{
				it = b.AvailableResourceList.erase(it);
				pDesc->pResource->Release();
#ifndef NDEBUG
				WriteDebugStringW(DEBUG_OUTPUT_TYPE_DEBUG_CONSOLE, L"D3DResource(%u Bytes) released\n", pDesc->MemorySize);
#endif
				SAFE_DELETE(pDesc);
			}
			else
			{
				++it;
			}
		}
	}
}
