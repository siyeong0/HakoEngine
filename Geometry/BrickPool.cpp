#include "pch.h"
#include "BrickPool.h"
#include <cassert>
#include <algorithm>
#include <d3d12.h>

BrickPool::BrickPool() = default;
BrickPool::~BrickPool()
{
    Shutdown();
}

bool BrickPool::Initialize(ID3D12Device* device, const Desc& desc)
{
    m_Desc = desc;

    // 슬롯 / 핸들 테이블 초기화
    m_Slots.resize(m_Desc.MaxBricks);
    for (SlotIndex i = 0; i < m_Desc.MaxBricks; ++i)
    {
        m_Slots[i].Handle = INVALID_HANDLE;
        m_Slots[i].LastUsedFrame = 0;
        m_Slots[i].Occupied = false;
        m_FreeSlots.push_back(i);
    }

    // GPU 리소스 생성 (Texture3DArray 등)
    if (!CreateGPUResources(device))
        return false;

    // 업로드 버퍼 등 필요하면 여기서 생성
    // ...

    return true;
}

void BrickPool::Shutdown()
{
    DestroyGPUResources();
    m_Slots.clear();
    m_HandleTable.clear();
    m_FreeSlots.clear();
    while (!m_RequestQueue.empty()) m_RequestQueue.pop();
    m_Backend = nullptr;
}

BrickPool::BrickHandle BrickPool::AllocateHandle()
{
    BrickHandle h = static_cast<BrickHandle>(m_HandleTable.size());
    HandleRecord rec{};
    rec.Slot = INVALID_SLOT;
    rec.Requested = false;
    m_HandleTable.push_back(rec);
    return h;
}

void BrickPool::FreeHandle(BrickHandle handle)
{
    if (handle >= m_HandleTable.size())
        return;

    HandleRecord& rec = m_HandleTable[handle];
    if (rec.Slot != INVALID_SLOT)
    {
        // 슬롯 비우기
        Slot& s = m_Slots[rec.Slot];
        s.Occupied = false;
        s.Handle = INVALID_HANDLE;
        m_FreeSlots.push_back(rec.Slot);
        rec.Slot = INVALID_SLOT;
    }

    // 여기서는 단순히 mark만 해두고 실제 shrink는 안 한다.
    // (나중에 compacting 하고 싶으면 별도 API에서)
}

void BrickPool::BeginFrame(uint64_t frameIndex)
{
    m_CurrentFrame = frameIndex;
    // per-frame state 초기화 필요하면 여기서
}

void BrickPool::RequestResidency(BrickHandle handle)
{
    if (handle == INVALID_HANDLE || handle >= m_HandleTable.size())
        return;

    HandleRecord& rec = m_HandleTable[handle];
    if (!rec.Requested)
    {
        rec.Requested = true;
        m_RequestQueue.push(handle);
    }
}

void BrickPool::ProcessStreaming(
    ID3D12GraphicsCommandList* uploadCmdList,
    uint32_t maxUploadsPerFrame)
{
    if (!m_Backend) return;
    if (!uploadCmdList) return;

    uint32_t uploads = 0;

    while (!m_RequestQueue.empty() && uploads < maxUploadsPerFrame)
    {
        BrickHandle handle = m_RequestQueue.front();
        m_RequestQueue.pop();

        if (handle >= m_HandleTable.size())
            continue;

        HandleRecord& rec = m_HandleTable[handle];
        rec.Requested = false;

        // 이미 resident면 LRU만 갱신
        if (rec.Slot != INVALID_SLOT)
        {
            Slot& s = m_Slots[rec.Slot];
            s.LastUsedFrame = m_CurrentFrame;
            continue;
        }

        // 아니면 새로 올림
        SlotIndex slot = MakeResidentInternal(handle, uploadCmdList);
        if (slot != INVALID_SLOT)
        {
            ++uploads;
        }
    }
}

BrickPool::SlotIndex BrickPool::TryResolveSlot(BrickHandle handle) const
{
    if (handle == INVALID_HANDLE || handle >= m_HandleTable.size())
        return INVALID_SLOT;
    const HandleRecord& rec = m_HandleTable[handle];
    return rec.Slot;
}

BrickPool::SlotIndex BrickPool::EnsureResident(
    BrickHandle handle,
    ID3D12GraphicsCommandList* uploadCmdList)
{
    if (handle == INVALID_HANDLE || handle >= m_HandleTable.size())
        return INVALID_SLOT;

    HandleRecord& rec = m_HandleTable[handle];
    if (rec.Slot != INVALID_SLOT)
    {
        Slot& s = m_Slots[rec.Slot];
        s.LastUsedFrame = m_CurrentFrame;
        return rec.Slot;
    }

    // 필요시 바로 업로드
    return MakeResidentInternal(handle, uploadCmdList);
}

BrickPool::SlotIndex BrickPool::MakeResidentInternal(
    BrickHandle handle,
    ID3D12GraphicsCommandList* uploadCmdList)
{
    assert(m_Backend && "IBrickStreamBackend must be set");

    // 1) 백엔드에서 CPU 데이터 가져오기
    IBrickStreamBackend::BrickCPUData cpuData;
    cpuData.Data.resize(
        m_Desc.BrickSize * m_Desc.BrickSize * m_Desc.BrickSize *
        m_Desc.BytesPerVoxel);

    if (!m_Backend->LoadBrickCPU(handle, cpuData))
    {
        // 로드 실패 → 그냥 실패 처리
        return INVALID_SLOT;
    }

    // 2) LRU로 슬롯 하나 확보
    SlotIndex slot = AllocateSlotLRU();
    if (slot == INVALID_SLOT)
        return INVALID_SLOT;

    // 3) GPU에 업로드
    //    - Texture3DArray라면 SubresourceIndex = slot
    //    - 3D atlas라면 x,y,z 오프셋 계산 필요
    //    아래는 pseudo-code:

    // D3D12_SUBRESOURCE_DATA subresource{};
    // subresource.pData      = cpuData.Data.data();
    // subresource.RowPitch   = m_Desc.BrickSize * m_Desc.BytesPerVoxel;
    // subresource.SlicePitch = subresource.RowPitch * m_Desc.BrickSize;
    //
    // UpdateSubresources(
    //     uploadCmdList,
    //     m_GPU.BrickTexture.Get(), m_UploadBuffer.Get(),
    //     /*dstOffset*/ 0,
    //     /*firstSubresource*/ slot,
    //     /*numSubresources*/ 1,
    //     &subresource);

    // 4) 슬롯/핸들 테이블 갱신
    Slot& s = m_Slots[slot];
    s.Handle = handle;
    s.Occupied = true;
    s.LastUsedFrame = m_CurrentFrame;

    HandleRecord& rec = m_HandleTable[handle];
    rec.Slot = slot;
    rec.Requested = false;

    return slot;
}

BrickPool::SlotIndex BrickPool::AllocateSlotLRU()
{
    // free slot 있으면 먼저 사용
    if (!m_FreeSlots.empty())
    {
        SlotIndex slot = m_FreeSlots.back();
        m_FreeSlots.pop_back();
        return slot;
    }

    // 없으면 LRU victim 하나 선택
    uint64_t oldestFrame = UINT64_MAX;
    SlotIndex victim = INVALID_SLOT;

    for (SlotIndex i = 0; i < m_Desc.MaxBricks; ++i)
    {
        const Slot& s = m_Slots[i];
        if (!s.Occupied)
        {
            // 이론상 여긴 안 들어오지만, safety
            return i;
        }
        if (s.LastUsedFrame < oldestFrame)
        {
            oldestFrame = s.LastUsedFrame;
            victim = i;
        }
    }

    if (victim != INVALID_SLOT)
    {
        EvictSlot(victim);
    }

    return victim;
}

void BrickPool::EvictSlot(SlotIndex slot)
{
    Slot& s = m_Slots[slot];
    if (!s.Occupied)
        return;

    BrickHandle handle = s.Handle;
    if (handle != INVALID_HANDLE && handle < m_HandleTable.size())
    {
        // 필요하면 현재 GPU 데이터를 읽어서 backend로 저장하는 로직 추가 가능
        // (보통은 CPU 쪽이 진실을 들고 있고 GPU는 캐시라서 이거 안 해도 됨)

        HandleRecord& rec = m_HandleTable[handle];
        rec.Slot = INVALID_SLOT;
    }

    s.Handle = INVALID_HANDLE;
    s.Occupied = false;
    s.LastUsedFrame = 0;
}

bool BrickPool::CreateGPUResources(ID3D12Device* device)
{
    // 여기서 Texture3D / Texture3DArray 생성
    // - Width  = BrickSize
    // - Height = BrickSize
    // - Depth  = BrickSize * MaxBricks  (3DArray면 ArraySize = MaxBricks)

    // D3D12_RESOURCE_DESC desc{};
    // desc.Dimension          = D3D12_RESOURCE_DIMENSION_TEXTURE3D (or 2D array);
    // desc.Width              = m_Desc.BrickSize;
    // desc.Height             = m_Desc.BrickSize;
    // desc.DepthOrArraySize   = m_Desc.MaxBricks; // Texture3DArray면 ArraySize
    // desc.MipLevels          = 1;
    // desc.Format             = m_Desc.Format;
    // desc.SampleDesc.Count   = 1;
    // desc.Layout             = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    // desc.Flags              = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS; // 필요시
    //
    // auto heapProps = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT);
    // HRESULT hr = device->CreateCommittedResource(
    //     &heapProps, D3D12_HEAP_FLAG_NONE,
    //     &desc,
    //     D3D12_RESOURCE_STATE_COMMON,
    //     nullptr,
    //     IID_PPV_ARGS(&m_GPU.BrickTexture));
    //
    // if (FAILED(hr)) return false;

    return true;
}

void BrickPool::DestroyGPUResources()
{
    m_GPU.BrickTexture.Reset();
    m_UploadBuffer.Reset();
}
