#pragma once

struct D3D12ResourceAllocDesc
{
	ID3D12Resource* pResource = nullptr;
	ULONGLONG RegisteredTick = 0; // 버킷(재사용 대기 리스트)에 들어간 시각
	size_t MemorySize = 0;         // 실제 리소스 크기(Width)
	int FrameLifeCount = 0; // pending 단계에서 frame 카운트
};

struct D3D12ResourceBucket
{
	size_t MemorySize = 0;                                      // 버킷 표준 크기(64KB, 128KB, ... 2^n)
	std::list<D3D12ResourceAllocDesc*> AvailableResourceList;          // 재사용 대기 중인 리소스들
};

class D3D12ResourceRecycleBin
{
public:
	D3D12ResourceRecycleBin() = default;;
	~D3D12ResourceRecycleBin() { Cleanup(); }

	void Initialize(
		ID3D12Device5* pD3DDevice,
		D3D12_HEAP_TYPE heapType,
		D3D12_RESOURCE_FLAGS resourceFlags,
		D3D12_RESOURCE_STATES initialResourceState,
		const WCHAR* wchResourceName);
	void Cleanup();

	ID3D12Resource* Alloc(size_t memSize);
	void Update(uint64_t currTick);
	void Free(ID3D12Resource* pResource, int frameLifeCount);

private:
	D3D12ResourceBucket* findBucket(size_t memSize);
	uint updatePendingResource(uint64_t currTick);
	void freeAllExpiredD3DResource(uint64_t currTick);

private:
	ID3D12Device5* m_pD3DDevice = nullptr;

	// 64KB에서 시작해 2배씩 커지는 버킷 16개
	std::vector<D3D12ResourceBucket> m_Buckets;            // size=16

	// Free()에서 넘어온 자원: 프레임 라이프 카운트가 0이 되면 버킷으로 이동
	std::list<D3D12ResourceAllocDesc*> m_PendingResourceList;

	D3D12_HEAP_TYPE m_HeapType = D3D12_HEAP_TYPE_UPLOAD;
	D3D12_RESOURCE_FLAGS m_ResourceFlags = D3D12_RESOURCE_FLAG_NONE;
	D3D12_RESOURCE_STATES m_InitialResouceState = D3D12_RESOURCE_STATE_GENERIC_READ;

	// 주기적 정리용
	uint64_t m_PrevFreedTick = 0;

	WCHAR m_wchResourceName[64] = {};
};
