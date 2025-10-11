#include "pch.h"
#include "ConvexDecomposition.h"
// ===============================================================
// Connected Components over GpuFriendlySparseGridFB (6-neighbor)
// - Tile-level graph (face-overlap) → BFS labeling
// - Per-component: tiles, voxelCount, surfaceCount, AABB(voxel index)
// ===============================================================

// ========================= POPCOUNT64 =========================
#ifndef POPCOUNT64
#  if defined(_MSC_VER)
#    include <intrin.h>
static inline uint32_t POPCOUNT64(uint64_t x) { return (uint32_t)__popcnt64(x); }
#  else
static inline uint32_t POPCOUNT64(uint64_t x) { return (uint32_t)__builtin_popcountll(x); }
#  endif
#endif

// ========================= Face layer (32x32 = 1024bit) =========================
struct FaceLayer1024 {
    static constexpr int WORDS = 1024 / 64; // 16
    uint64_t w[WORDS];
    void clear() { for (int i = 0; i < WORDS; ++i) w[i] = 0ull; }
    void setAll() { for (int i = 0; i < WORDS; ++i) w[i] = ~0ull; }
    bool any() const { uint64_t acc = 0; for (int i = 0; i < WORDS; ++i) acc |= w[i]; return acc != 0ull; }
    uint32_t popcnt() const { uint32_t s = 0; for (int i = 0; i < WORDS; ++i) s += POPCOUNT64(w[i]); return s; }
    static uint32_t popcntAND(const FaceLayer1024& a, const FaceLayer1024& b) {
        uint32_t s = 0; for (int i = 0; i < WORDS; ++i) s += POPCOUNT64(a.w[i] & b.w[i]); return s;
    }
};

// face 식별자
enum FaceDir { XMIN = 0, XMAX = 1, YMIN = 2, YMAX = 3, ZMIN = 4, ZMAX = 5 };

// ========================= 타일의 face 레이어 추출 =========================
// TileCPU::Bits 인덱스: li = x | (y<<5) | (z<<10), x,y,z ∈ [0..31]
static inline void ExtractFaceLayer(const TileCPU& t, FaceDir f, FaceLayer1024& out)
{
    out.clear();
    if (t.Mode == TileCPU::FULL) { out.setAll(); return; }

    if (f == XMIN || f == XMAX) {
        const int x = (f == XMIN) ? 0 : 31;
        int bitIdx = 0;
        for (int z = 0; z < 32; ++z)
            for (int y = 0; y < 32; ++y) {
                const uint16_t li = (uint16_t)(x | (y << 5) | (z << 10));
                const uint64_t bit = (t.Bits[li >> 6] >> (li & 63)) & 1ull;
                if (bit) out.w[bitIdx >> 6] |= (1ull << (bitIdx & 63));
                ++bitIdx;
            }
        return;
    }
    if (f == YMIN || f == YMAX) {
        const int y = (f == YMIN) ? 0 : 31;
        int bitIdx = 0;
        for (int z = 0; z < 32; ++z)
            for (int x = 0; x < 32; ++x) {
                const uint16_t li = (uint16_t)(x | (y << 5) | (z << 10));
                const uint64_t bit = (t.Bits[li >> 6] >> (li & 63)) & 1ull;
                if (bit) out.w[bitIdx >> 6] |= (1ull << (bitIdx & 63));
                ++bitIdx;
            }
        return;
    }
    { // ZMIN / ZMAX
        const int z = (f == ZMIN) ? 0 : 31;
        int bitIdx = 0;
        for (int y = 0; y < 32; ++y)
            for (int x = 0; x < 32; ++x) {
                const uint16_t li = (uint16_t)(x | (y << 5) | (z << 10));
                const uint64_t bit = (t.Bits[li >> 6] >> (li & 63)) & 1ull;
                if (bit) out.w[bitIdx >> 6] |= (1ull << (bitIdx & 63));
                ++bitIdx;
            }
        return;
    }
}

// 두 타일의 맞은편 face가 1비트라도 겹치면 연결
static inline bool TilesFaceConnected(const TileCPU& A, FaceDir fA, const TileCPU& B, FaceDir fB)
{
    if (A.Mode == TileCPU::FULL && B.Mode == TileCPU::FULL) return true;
    FaceLayer1024 LA, LB; ExtractFaceLayer(A, fA, LA); ExtractFaceLayer(B, fB, LB);
    return FaceLayer1024::popcntAND(LA, LB) > 0;
}

// ========================= 타일 로컬 AABB (정밀, 반-열린 [min,max)) =========================
static inline bool GetTileLocalAABB(const TileCPU& t,
    int& lx0, int& ly0, int& lz0,
    int& lx1, int& ly1, int& lz1) // [min, max) in [0,32]
{
    if (t.Mode == TileCPU::FULL) {
        lx0 = ly0 = lz0 = 0;
        lx1 = ly1 = lz1 = 32;
        return true;
    }

    // BITSET: 비트가 하나도 없을 수도 있으니 안전 확인
    int minx = 32, miny = 32, minz = 32;
    int maxx = -1, maxy = -1, maxz = -1;

    // Bits는 512 words(=32768 bits). 세트 비트만 스캔.
    for (int wi = 0; wi < TileCPU::BITSET_WORDS; ++wi) {
        uint64_t w = t.Bits[(size_t)wi];
        while (w) {
#if defined(_MSC_VER)
            unsigned long b; _BitScanForward64(&b, w);
            int bit = (int)b;
#else
            int bit = __builtin_ctzll(w);
#endif
            const int g = (wi << 6) + bit; // 0..32767
            const int x = g & 31;
            const int y = (g >> 5) & 31;
            const int z = (g >> 10) & 31;

            if (x < minx) minx = x; if (x > maxx) maxx = x;
            if (y < miny) miny = y; if (y > maxy) maxy = y;
            if (z < minz) minz = z; if (z > maxz) maxz = z;

            w &= (w - 1); // clear lowest set bit
        }
    }

    if (maxx < 0) return false; // empty

    lx0 = minx; ly0 = miny; lz0 = minz;
    lx1 = maxx + 1; ly1 = maxy + 1; lz1 = maxz + 1; // 반-열린 상한
    return true;
}

// ========================= 연결 성분 추출 (6-이웃) =========================
void ExtractConnectedComponents6(
    const GpuFriendlySparseGridFB& solid,
    std::vector<VoxelComponent>& outComponents)
{
    // 0) 타일이 없으면 종료
    const size_t numTiles = solid.TileVector.size();
    if (numTiles == 0) { outComponents.clear(); return; }

    // 1) 타일 인덱스 -> (tx,ty,tz) 좌표 맵 구성 (인덱스 접근 O(1)용)
    struct TileCoord { int tx, ty, tz; };
    std::vector<TileCoord> coordLUT(numTiles, { INT32_MAX, INT32_MAX, INT32_MAX });

    // forEachTile은 해시 슬롯 순회지만, 내부 val(=tileIdx)와 (tx,ty,tz)를 준다.
    solid.forEachTile([&](uint64_t /*key*/, int v, int tx, int ty, int tz) {
        if ((size_t)v < numTiles) coordLUT[(size_t)v] = { tx, ty, tz };
        });

    // 2) 방문 배열은 타일 인덱스 기준
    std::vector<uint8_t> visited(numTiles, 0);

    // 3) 결과 컨테이너 준비
    outComponents.clear();
    outComponents.reserve(numTiles);

    // 6-이웃 + face 매핑
    const int d6[6][3] = { {+1,0,0},{-1,0,0},{0,+1,0},{0,-1,0},{0,0,+1},{0,0,-1} };
    const FaceDir facePos[6] = { XMAX, XMIN, YMAX, YMIN, ZMAX, ZMIN };
    const FaceDir faceNeg[6] = { XMIN, XMAX, YMIN, YMAX, ZMIN, ZMAX };

    // 4) 모든 타일을 시작점으로 BFS
    for (size_t startIdx = 0; startIdx < numTiles; ++startIdx) {
        if (visited[startIdx]) continue;

        // coordLUT가 채워지지 않은 빈 슬롯은 스킵
        const TileCoord c0 = coordLUT[startIdx];
        if (c0.tx == INT32_MAX) { visited[startIdx] = 1; continue; }

        // 새 컴포넌트
        VoxelComponent comp;
        comp.id = (int)outComponents.size();

        // 반-열린 AABB 초기화 (정확한 누적을 위해 first-hit 시 갱신)
        comp.minX = comp.minY = comp.minZ = INT32_MAX;
        comp.maxX = comp.maxY = comp.maxZ = -INT32_MAX;

        // BFS 큐: 타일 인덱스 보관
        std::vector<int> q; q.reserve(64);
        q.push_back((int)startIdx);
        visited[startIdx] = 1;

        while (!q.empty()) {
            const int curIdx = q.back(); q.pop_back();

            const TileCPU& Tcur = solid.TileVector[(size_t)curIdx];
            const TileCoord tc = coordLUT[(size_t)curIdx];

            // 통계: 타일 목록, 복셀 수
            comp.tiles.push_back({ tc.tx, tc.ty, tc.tz });
            comp.voxelCount += (Tcur.Mode == TileCPU::FULL) ? (uint64_t)TileCPU::TILE_VOXELS
                : (uint64_t)Tcur.Count;

            // 정밀 AABB 누적 (반-열린)
            {
                int lx0, ly0, lz0, lx1, ly1, lz1;
                if (GetTileLocalAABB(Tcur, lx0, ly0, lz0, lx1, ly1, lz1)) {
                    const int baseX = tc.tx << 5;
                    const int baseY = tc.ty << 5;
                    const int baseZ = tc.tz << 5;

                    const int gx0 = baseX + lx0;
                    const int gy0 = baseY + ly0;
                    const int gz0 = baseZ + lz0;

                    const int gx1 = baseX + lx1; // 반-열린
                    const int gy1 = baseY + ly1;
                    const int gz1 = baseZ + lz1;

                    comp.minX = std::min(comp.minX, gx0);
                    comp.minY = std::min(comp.minY, gy0);
                    comp.minZ = std::min(comp.minZ, gz0);
                    comp.maxX = std::max(comp.maxX, gx1);
                    comp.maxY = std::max(comp.maxY, gy1);
                    comp.maxZ = std::max(comp.maxZ, gz1);
                }
            }

            // 6방향 이웃 확장
            for (int k = 0; k < 6; ++k) {
                const int ntx = tc.tx + d6[k][0];
                const int nty = tc.ty + d6[k][1];
                const int ntz = tc.tz + d6[k][2];

                const int neiIdx = solid.findTileIndex(ntx, nty, ntz);
                if (neiIdx < 0) continue;
                if (visited[(size_t)neiIdx]) continue;

                const TileCPU& Tnei = solid.TileVector[(size_t)neiIdx];

                // face-overlap 체크 (A: cur의 +dir face, B: nei의 -dir face)
                if (TilesFaceConnected(Tcur, facePos[k], Tnei, faceNeg[k])) {
                    visited[(size_t)neiIdx] = 1;
                    q.push_back(neiIdx);
                }
            }
        }

        // 표면 복셀 수 계산:
        // 각 타일의 6면에 대해, "이웃 타일의 맞은편 face와 겹치지 않는 부분"만 표면으로 카운트
        uint64_t surf = 0;
        for (const auto& tc : comp.tiles) {
            uint64_t faceOut[6] = { 0,0,0,0,0,0 };
            const int selfIdx = solid.findTileIndex(tc.tx, tc.ty, tc.tz);
            if (selfIdx < 0) continue; // 안전장치
            const TileCPU& Tcur = solid.TileVector[(size_t)selfIdx];

            for (int k = 0; k < 6; ++k) {
                const int ntx = tc.tx + d6[k][0];
                const int nty = tc.ty + d6[k][1];
                const int ntz = tc.tz + d6[k][2];
                const int nidx = solid.findTileIndex(ntx, nty, ntz);

                if (Tcur.Mode == TileCPU::FULL) {
                    if (nidx < 0) {
                        faceOut[k] = 32u * 32u; // 전부 외부
                    }
                    else {
                        const TileCPU& Tnei = solid.TileVector[(size_t)nidx];
                        if (Tnei.Mode == TileCPU::FULL) {
                            faceOut[k] = 0; // 전부 내부
                        }
                        else {
                            FaceLayer1024 Nf; ExtractFaceLayer(Tnei, faceNeg[k], Nf);
                            const uint32_t nOn = Nf.popcnt();
                            faceOut[k] = (uint64_t)(32u * 32u - nOn);
                        }
                    }
                }
                else { // BITSET
                    FaceLayer1024 Cf; ExtractFaceLayer(Tcur, facePos[k], Cf);
                    if (nidx < 0) {
                        faceOut[k] = Cf.popcnt();
                    }
                    else {
                        const TileCPU& Tnei = solid.TileVector[(size_t)nidx];
                        if (Tnei.Mode == TileCPU::FULL) {
                            faceOut[k] = 0; // 전부 내부
                        }
                        else {
                            FaceLayer1024 Nf; ExtractFaceLayer(Tnei, faceNeg[k], Nf);
                            const uint32_t overlap = FaceLayer1024::popcntAND(Cf, Nf);
                            faceOut[k] = (uint64_t)Cf.popcnt() - (uint64_t)overlap;
                        }
                    }
                }
            }
            surf += faceOut[0] + faceOut[1] + faceOut[2] + faceOut[3] + faceOut[4] + faceOut[5];
        }
        comp.surfaceCount = surf;

        outComponents.push_back(std::move(comp));
    }
}
