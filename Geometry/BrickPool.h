#pragma once
#include <cstdint>
#include <vector>
#include <queue>
#include <limits>
#include <functional>
#include <wrl/client.h> // Microsoft::WRL::ComPtr

struct ID3D12Device;
struct ID3D12GraphicsCommandList;
struct ID3D12Resource;
enum DXGI_FORMAT : int;

using Microsoft::WRL::ComPtr;

// ----------------------------------------------------------------------------
// Brick 스트리밍 백엔드 인터페이스
//  - 디스크/CPU 메모리에서 브릭 데이터를 로드/스토어하는 역할
//  - BrickPool은 "이 핸들의 브릭 CPU 데이터를 채워줘" 정도만 요구하고,
//    실제 구현은 파일 I/O, 압축 등 자유롭게 구성 가능.
// ----------------------------------------------------------------------------
class IBrickStreamBackend
{
public:
    virtual ~IBrickStreamBackend() = default;

    struct BrickCPUData
    {
        // BRICK_SIZE^3 * bytesPerVoxel 만큼의 데이터를 담을 버퍼
        // (실제 포맷은 엔진에서 정의. 예: R8_UNORM, R16_FLOAT, RGBA8 등)
        std::vector<uint8_t> Data;
    };

    // 특정 BrickHandle의 CPU 데이터를 채워준다.
    // - 없는 브릭이면 생성해서 채워도 되고, 에러를 내도 됨 (정책에 따라)
    virtual bool LoadBrickCPU(uint32_t brickHandle, BrickCPUData& outData) = 0;

    // GPU에서 evict 되기 전에 CPU/디스크에 저장해야 하면 여기에.
    // - 안 쓰고 싶으면 no-op 구현하면 됨.
    virtual void StoreBrickCPU(uint32_t brickHandle, const BrickCPUData& data) = 0;
};

// ----------------------------------------------------------------------------
// BrickPool
//  - 고정 크기 GPU brick 풀 (Texture3DArray or tiled Texture3D)
//  - BrickHandle ↔ SlotIndex 매핑 + LRU eviction
//  - CPU/디스크 스트리밍은 IBrickStreamBackend를 통해 처리
// ----------------------------------------------------------------------------
class BrickPool
{
public:
    // logical handle (노드가 들고 있는 값)
    using BrickHandle = uint32_t;

    // GPU slot index (Texture3DArray에서 brick 인덱스로 사용)
    using SlotIndex = uint32_t;

    static constexpr BrickHandle INVALID_HANDLE =
        std::numeric_limits<BrickHandle>::max();
    static constexpr SlotIndex INVALID_SLOT =
        std::numeric_limits<SlotIndex>::max();

    struct Desc
    {
        uint32_t BrickSize = 8;          // 8x8x8 등
        uint32_t MaxBricks = 1024;       // GPU brick slot 개수
        DXGI_FORMAT Format = (DXGI_FORMAT)0; // 브릭 텍스처 포맷
        uint32_t BytesPerVoxel = 1;          // CPU 버퍼용
        uint32_t BricksPerRow = 0;          // Texture3DArray 쓰면 필요 X
        bool     UseArrayTexture = true;       // Texture3DArray vs 3D atlas
    };

    struct GPUResources
    {
        ComPtr<ID3D12Resource> BrickTexture;  // Texture3D / Texture3DArray
        // 필요하면 SRV/ UAV/ RTV 등도 여기에
    };

    // 슬롯 상태
    struct Slot
    {
        BrickHandle Handle = INVALID_HANDLE;
        uint64_t    LastUsedFrame = 0; // LRU용
        bool        Occupied = false;
    };

    // 핸들 테이블 엔트리
    struct HandleRecord
    {
        SlotIndex Slot = INVALID_SLOT; // GPU 상주 여부
        bool      Requested = false;        // 현재 프레임에 residency 요청됨
    };

public:
    BrickPool();
    ~BrickPool();

    // 초기화: GPU 리소스 생성
    bool Initialize(ID3D12Device* device, const Desc& desc);

    // 해제
    void Shutdown();

    const Desc& GetDesc() const { return m_Desc; }
    const GPUResources& GetGPUResources() const { return m_GPU; }

    // 스트리밍 백엔드 설정 (디스크/CPU)
    void SetBackend(IBrickStreamBackend* backend) { m_Backend = backend; }

    // 새 논리 핸들 생성: 이 핸드는 VolumeNode에서 BrickBits로 사용하면 됨
    BrickHandle AllocateHandle();
    void        FreeHandle(BrickHandle handle);

    // 현재 프레임 번호 세팅 (LRU용)
    void BeginFrame(uint64_t frameIndex);

    // 이 핸들이 GPU에 필요하다고 표시
    // - 실제로 즉시 업로드하지 않고 요청 큐에 넣어두었다가
    //   EndFrame 혹은 ProcessStreaming에서 처리
    void RequestResidency(BrickHandle handle);

    // 요청된 브릭들을 처리해서 GPU에 올림
    // - uploadCmdList: 업로드용 커맨드 리스트
    // - maxUploadsPerFrame: 한 프레임에 올릴 수 있는 브릭 개수 제한
    void ProcessStreaming(
        ID3D12GraphicsCommandList* uploadCmdList,
        uint32_t maxUploadsPerFrame = 16);

    // 핸들이 이미 resident라면 그 slotIndex 반환, 아니면 INVALID_SLOT
    SlotIndex TryResolveSlot(BrickHandle handle) const;

    // "이 핸들에 대한 GPU brick index를 꼭 알고 싶다"는 경우:
    // - resident가 아니면 여기서 바로 MakeResident까지 수행
    // - 단, 이 함수는 stalling / IO를 유발할 수 있으니
    //   진짜 필요한 곳에서만 사용하고, 보통은 RequestResidency + streaming을 추천
    SlotIndex EnsureResident(
        BrickHandle handle,
        ID3D12GraphicsCommandList* uploadCmdList);

    // 디버깅용 accessor
    const std::vector<Slot>& GetSlots() const { return m_Slots; }
    const std::vector<HandleRecord>& GetHandles() const { return m_HandleTable; }

private:
    // 내부: 실제로 브릭을 GPU에 올리는 함수 (슬롯 할당 + 업로드)
    SlotIndex MakeResidentInternal(
        BrickHandle handle,
        ID3D12GraphicsCommandList* uploadCmdList);

    // 내부: LRU 기반으로 사용 가능한 슬롯 하나 가져오기
    SlotIndex AllocateSlotLRU();

    // 내부: 슬롯 eviction 처리
    void EvictSlot(SlotIndex slot);

    // GPU 리소스 생성/해제
    bool CreateGPUResources(ID3D12Device* device);
    void DestroyGPUResources();

private:
    Desc         m_Desc{};
    GPUResources m_GPU{};

    std::vector<Slot>         m_Slots;
    std::vector<HandleRecord> m_HandleTable;

    // free slot 리스트 (초기엔 0..MaxBricks-1)
    std::vector<SlotIndex>    m_FreeSlots;

    // streaming 요청 큐
    std::queue<BrickHandle>   m_RequestQueue;

    IBrickStreamBackend* m_Backend = nullptr; // not owned

    uint64_t                  m_CurrentFrame = 0;

    // D3D12 업로드용 scratch 리소스 등 필요한 것들
    ComPtr<ID3D12Resource>    m_UploadBuffer; // 예시
};
