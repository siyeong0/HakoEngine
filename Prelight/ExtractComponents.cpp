#include "pch.h"
#include "ConvexDecomposition.h"
// ===============================================================
// Connected Components over GpuFriendlySparseGridFB (6-neighbor)
// - Tile-level graph (face-overlap) → BFS labeling
// - Per-component: tiles, voxelCount, surfaceCount, AABB(voxel index)
// ===============================================================

#ifndef POPCOUNT64
#if defined(_MSC_VER)
#include <intrin.h>
static inline uint32_t POPCOUNT64(uint64_t x) { return (uint32_t)__popcnt64(x); }
#else
static inline uint32_t POPCOUNT64(uint64_t x) { return (uint32_t)__builtin_popcountll(x); }
#endif
#endif

// --- (2) face 레이어(32x32=1024bit) 추출 도구 ------------------------
struct FaceLayer1024 {
	static constexpr int WORDS = 1024 / 64; // 16
	uint64_t w[WORDS];
	void clear() { for (int i = 0; i < WORDS; ++i) w[i] = 0ull; }
	void setAll() { for (int i = 0; i < WORDS; ++i) w[i] = ~0ull; }
	bool any() const {
		uint64_t acc = 0; for (int i = 0; i < WORDS; ++i) acc |= w[i]; return acc != 0ull;
	}
	uint32_t popcnt() const {
		uint32_t s = 0; for (int i = 0; i < WORDS; ++i) s += POPCOUNT64(w[i]); return s;
	}
	static uint32_t popcntAND(const FaceLayer1024& a, const FaceLayer1024& b) {
		uint32_t s = 0; for (int i = 0; i < WORDS; ++i) s += POPCOUNT64(a.w[i] & b.w[i]); return s;
	}
};

// face 식별자
enum FaceDir { XMIN = 0, XMAX = 1, YMIN = 2, YMAX = 3, ZMIN = 4, ZMAX = 5 };

// 타일에서 face 레이어(1024bit) 추출
// - FULL: 올원
// - BITSET: 로컬 인덱스(li)로 해당 면(x=0 or 31 등)만 모아 1024 비트로 구성
static inline void ExtractFaceLayer(const TileCPU& t, FaceDir f, FaceLayer1024& out)
{
	using T = TileCPU;
	out.clear();
	if (t.Mode == T::FULL) { out.setAll(); return; }

	// BITSET: (x,y,z) in [0..31]^3
	// li = x | (y<<5) | (z<<10)
	// face는 x==0/x==31, y==0/y==31, z==0/z==31 중 하나
	if (f == XMIN || f == XMAX) {
		const int x = (f == XMIN) ? 0 : 31;
		// 32x32 plane: index into out.w is row-major over (y,z) or (z,y) 상관없음 (일관성만 유지)
		int bitIdx = 0;
		for (int z = 0; z < 32; ++z) {
			for (int y = 0; y < 32; ++y) {
				const uint16_t li = (uint16_t)(x | (y << 5) | (z << 10));
				const uint64_t bit = ((uint64_t)t.Bits[li >> 6] >> (li & 63)) & 1ull;
				if (bit) out.w[bitIdx >> 6] |= (1ull << (bitIdx & 63));
				++bitIdx;
			}
		}
		return;
	}
	if (f == YMIN || f == YMAX) {
		const int y = (f == YMIN) ? 0 : 31;
		int bitIdx = 0;
		for (int z = 0; z < 32; ++z) {
			for (int x = 0; x < 32; ++x) {
				const uint16_t li = (uint16_t)(x | (y << 5) | (z << 10));
				const uint64_t bit = ((uint64_t)t.Bits[li >> 6] >> (li & 63)) & 1ull;
				if (bit) out.w[bitIdx >> 6] |= (1ull << (bitIdx & 63));
				++bitIdx;
			}
		}
		return;
	}
	{ // ZMIN/ZMAX
		const int z = (f == ZMIN) ? 0 : 31;
		int bitIdx = 0;
		for (int y = 0; y < 32; ++y) {
			for (int x = 0; x < 32; ++x) {
				const uint16_t li = (uint16_t)(x | (y << 5) | (z << 10));
				const uint64_t bit = ((uint64_t)t.Bits[li >> 6] >> (li & 63)) & 1ull;
				if (bit) out.w[bitIdx >> 6] |= (1ull << (bitIdx & 63));
				++bitIdx;
			}
		}
		return;
	}
}

// --- (3) 타일 face-overlap 검사 (A의 +X face vs B의 -X face 등) ----------
static inline bool TilesFaceConnected(const TileCPU& A, FaceDir fA, const TileCPU& B, FaceDir fB)
{
	// FULL/FULL인 경우는 무조건 연결
	if (A.Mode == TileCPU::FULL && B.Mode == TileCPU::FULL) return true;

	FaceLayer1024 LA, LB;
	ExtractFaceLayer(A, fA, LA);
	ExtractFaceLayer(B, fB, LB);
	return FaceLayer1024::popcntAND(LA, LB) > 0;
}

// --- (5) 메인: 연결 성분 추출 ---------------------------------------------
//static inline void ExtractConnectedComponents6(
//	const GpuFriendlySparseGridFB& solid,
//	std::vector<VoxelComponent>& outComponents)
//{
//	// 1) 활성 타일 목록 수집
//	struct TileInfo { uint64_t key; int idx; int tx, ty, tz; };
//	std::vector<TileInfo> tiles; tiles.reserve(solid.Size);
//	solid.forEachTile([&](uint64_t key, int idx, int tx, int ty, int tz) {
//		tiles.push_back({ key, idx, tx,ty,tz });
//		});
//	const int N = (int)tiles.size();
//	if (N == 0) { outComponents.clear(); return; }
//
//	// 2) key→order 매핑 (인접 타일 조회용)
//	//  - 여기서는 해시 대신 간단히 정렬 + lower_bound 사용
//	std::vector<uint64_t> keys(N);
//	for (int i = 0; i < N; ++i) keys[i] = tiles[i].key;
//	std::vector<int> order(N); // tiles[i]가 정렬 후 몇 번째인지
//	std::vector<int> rindex(N);// 정렬된 곳에서 원본 i는 무엇인지
//	std::vector<uint64_t> sorted = keys;
//	std::sort(sorted.begin(), sorted.end());
//	auto keyToSortedIdx = [&](uint64_t k)->int {
//		auto it = std::lower_bound(sorted.begin(), sorted.end(), k);
//		if (it == sorted.end() || *it != k) return -1;
//		return (int)(it - sorted.begin());
//		};
//	for (int i = 0; i < N; ++i) {
//		order[i] = keyToSortedIdx(keys[i]);
//	}
//	rindex = std::vector<int>(N, -1);
//	for (int i = 0; i < N; ++i) rindex[order[i]] = i;
//
//	auto pack3x21_local = [](int tx, int ty, int tz)->uint64_t {
//		const uint64_t B = 1ull << 20;
//		return ((uint64_t)((int64_t)tx + (int64_t)B)) |
//			((uint64_t)((int64_t)ty + (int64_t)B) << 21) |
//			((uint64_t)((int64_t)tz + (int64_t)B) << 42);
//		};
//
//	// 3) 방문 배열 (타일 단위)
//	std::vector<uint8_t> visited(N, 0);
//
//	// 4) BFS로 컴포넌트 만들기
//	outComponents.clear();
//	outComponents.reserve(N);
//
//	auto pushTileAABB = [](VoxelComponent& c, int tx, int ty, int tz) {
//		// 타일 경계(복셀 인덱스): [tx*32 .. tx*32+31]
//		int x0 = tx << 5, y0 = ty << 5, z0 = tz << 5;
//		int x1 = x0 + 31, y1 = y0 + 31, z1 = z0 + 31;
//		c.minX = std::min(c.minX, x0); c.minY = std::min(c.minY, y0); c.minZ = std::min(c.minZ, z0);
//		c.maxX = std::max(c.maxX, x1); c.maxY = std::max(c.maxY, y1); c.maxZ = std::max(c.maxZ, z1);
//		};
//
//	auto tileAtSorted = [&](int sortedIdx)->const TileInfo& {
//		return tiles[rindex[sortedIdx]];
//		};
//
//	// 이웃 방향과 상반되는 면 매핑
//	const int d6[6][3] = { {+1,0,0},{-1,0,0},{0,+1,0},{0,-1,0},{0,0,+1},{0,0,-1} };
//	const FaceDir facePos[6] = { XMAX, XMIN, YMAX, YMIN, ZMAX, ZMIN };
//	const FaceDir faceNeg[6] = { XMIN, XMAX, YMIN, YMAX, ZMIN, ZMAX };
//
//	for (int start = 0; start < N; ++start) {
//		if (visited[start]) continue;
//
//		// 새 컴포넌트 시작
//		VoxelComponent comp;
//		comp.id = (int)outComponents.size();
//
//		// 타일 BFS
//		std::vector<int> q; q.reserve(64);
//		q.push_back(start); visited[start] = 1;
//
//		while (!q.empty()) {
//			int cur = q.back(); q.pop_back();
//			const TileInfo& ti = tiles[cur];
//			const TileCPU& Tcur = solid.TileVector[(size_t)ti.idx];
//
//			// 통계
//			comp.tiles.push_back({ ti.tx,ti.ty,ti.tz });
//			comp.voxelCount += (Tcur.Mode == TileCPU::FULL) ? (uint64_t)TileCPU::TILE_VOXELS
//				: (uint64_t)Tcur.Count;
//			pushTileAABB(comp, ti.tx, ti.ty, ti.tz);
//
//			// 6방향 이웃 검사
//			for (int k = 0; k < 6; ++k) {
//				int ntx = ti.tx + d6[k][0];
//				int nty = ti.ty + d6[k][1];
//				int ntz = ti.tz + d6[k][2];
//				uint64_t nkey = pack3x21_local(ntx, nty, ntz);
//				int sidx = keyToSortedIdx(nkey);
//				if (sidx < 0) continue;
//				int nei = rindex[sidx];
//				if (visited[nei]) continue;
//
//				const TileCPU& Tnei = solid.TileVector[(size_t)tiles[nei].idx];
//				// face-overlap 체크: Tcur(facePos[k]) ∧ Tnei(faceNeg[k]) 가 1 이상인지
//				if (TilesFaceConnected(Tcur, facePos[k], Tnei, faceNeg[k])) {
//					visited[nei] = 1;
//					q.push_back(nei);
//				}
//			}
//		}
//
//		// surface voxel 수 계산
//		// 각 타일의 6면에 대해, "이웃 타일의 맞은편 face와 겹치지 않는 부분"만 표면으로 카운트
//		uint64_t surf = 0;
//		for (auto& tc : comp.tiles) {
//			uint64_t faceOut[6] = { 0,0,0,0,0,0 }; // 이 타일 각 face에서 외부로 노출되는 수(최대 1024)
//			const TileCPU& Tcur = solid.TileVector[(size_t)solid.findTileIndex(tc.tx, tc.ty, tc.tz)];
//
//			for (int k = 0; k < 6; ++k) {
//				int ntx = tc.tx + d6[k][0];
//				int nty = tc.ty + d6[k][1];
//				int ntz = tc.tz + d6[k][2];
//				int nidx = solid.findTileIndex(ntx, nty, ntz);
//
//				if (Tcur.Mode == TileCPU::FULL) {
//					if (nidx < 0) { faceOut[k] = 32u * 32u; } // 외부와 접함 → 전부 표면
//					else {
//						const TileCPU& Tnei = solid.TileVector[(size_t)nidx];
//						if (Tnei.Mode == TileCPU::FULL) {
//							faceOut[k] = 0; // 내부
//						}
//						else {
//							// 이웃이 BITSET → 이웃 face의 1의 개수만큼 내부, 나머지 표면
//							FaceLayer1024 Nf; ExtractFaceLayer(Tnei, faceNeg[k], Nf);
//							uint32_t nOn = Nf.popcnt();
//							faceOut[k] = (uint64_t)(32u * 32u - nOn);
//						}
//					}
//				}
//				else { // BITSET
//					FaceLayer1024 Cf; ExtractFaceLayer(Tcur, facePos[k], Cf);
//					if (nidx < 0) {
//						faceOut[k] = Cf.popcnt(); // 전부 외부로 노출
//					}
//					else {
//						const TileCPU& Tnei = solid.TileVector[(size_t)nidx];
//						if (Tnei.Mode == TileCPU::FULL) {
//							faceOut[k] = 0; // 전부 내부
//						}
//						else {
//							FaceLayer1024 Nf; ExtractFaceLayer(Tnei, faceNeg[k], Nf);
//							// Cf & (~Nf) 가 표면. = Cf.popcnt() - popcnt(Cf&Nf)
//							uint32_t overlap = FaceLayer1024::popcntAND(Cf, Nf);
//							faceOut[k] = (uint64_t)Cf.popcnt() - (uint64_t)overlap;
//						}
//					}
//				}
//			}
//			surf += faceOut[0] + faceOut[1] + faceOut[2] + faceOut[3] + faceOut[4] + faceOut[5];
//		}
//		comp.surfaceCount = surf;
//
//		outComponents.push_back(std::move(comp));
//	}
//}
void ExtractConnectedComponents6(
	const GpuFriendlySparseGridFB& solid,
	std::vector<VoxelComponent>& outComponents)
{
	// 1) 활성 타일 목록 수집
	struct TileInfo { uint64_t key; int idx; int tx, ty, tz; };
	std::vector<TileInfo> tiles; tiles.reserve(solid.Size);
	solid.forEachTile([&](uint64_t key, int idx, int tx, int ty, int tz) {
		tiles.push_back({ key, idx, tx,ty,tz });
		});
	const int N = (int)tiles.size();
	if (N == 0) { outComponents.clear(); return; }

	// 2) key→order 매핑 (인접 타일 조회용)
	//  - 여기서는 해시 대신 간단히 정렬 + lower_bound 사용
	std::vector<uint64_t> keys(N);
	for (int i = 0; i < N; ++i) keys[i] = tiles[i].key;
	std::vector<int> order(N); // tiles[i]가 정렬 후 몇 번째인지
	std::vector<int> rindex(N);// 정렬된 곳에서 원본 i는 무엇인지
	std::vector<uint64_t> sorted = keys;
	std::sort(sorted.begin(), sorted.end());
	auto keyToSortedIdx = [&](uint64_t k)->int {
		auto it = std::lower_bound(sorted.begin(), sorted.end(), k);
		if (it == sorted.end() || *it != k) return -1;
		return (int)(it - sorted.begin());
		};
	for (int i = 0; i < N; ++i) {
		order[i] = keyToSortedIdx(keys[i]);
	}
	rindex = std::vector<int>(N, -1);
	for (int i = 0; i < N; ++i) rindex[order[i]] = i;

	auto pack3x21_local = [](int tx, int ty, int tz)->uint64_t {
		const uint64_t B = 1ull << 20;
		return ((uint64_t)((int64_t)tx + (int64_t)B)) |
			((uint64_t)((int64_t)ty + (int64_t)B) << 21) |
			((uint64_t)((int64_t)tz + (int64_t)B) << 42);
		};

	// 3) 방문 배열 (타일 단위)
	std::vector<uint8_t> visited(N, 0);

	// 4) BFS로 컴포넌트 만들기
	outComponents.clear();
	outComponents.reserve(N);

	auto pushTileAABB = [](VoxelComponent& c, int tx, int ty, int tz) {
		// 타일 경계(복셀 인덱스): [tx*32 .. tx*32+31]
		int x0 = tx << 5, y0 = ty << 5, z0 = tz << 5;
		int x1 = x0 + 31, y1 = y0 + 31, z1 = z0 + 31;
		c.minX = std::min(c.minX, x0); c.minY = std::min(c.minY, y0); c.minZ = std::min(c.minZ, z0);
		c.maxX = std::max(c.maxX, x1); c.maxY = std::max(c.maxY, y1); c.maxZ = std::max(c.maxZ, z1);
		};

	auto tileAtSorted = [&](int sortedIdx)->const TileInfo& {
		return tiles[rindex[sortedIdx]];
		};

	// 이웃 방향과 상반되는 면 매핑
	const int d6[6][3] = { {+1,0,0},{-1,0,0},{0,+1,0},{0,-1,0},{0,0,+1},{0,0,-1} };
	const FaceDir facePos[6] = { XMAX, XMIN, YMAX, YMIN, ZMAX, ZMIN };
	const FaceDir faceNeg[6] = { XMIN, XMAX, YMIN, YMAX, ZMIN, ZMAX };

	for (int start = 0; start < N; ++start) {
		if (visited[start]) continue;

		// 새 컴포넌트 시작
		VoxelComponent comp;
		comp.id = (int)outComponents.size();

		// 타일 BFS
		std::vector<int> q; q.reserve(64);
		q.push_back(start); visited[start] = 1;

		while (!q.empty()) {
			int cur = q.back(); q.pop_back();
			const TileInfo& ti = tiles[cur];
			const TileCPU& Tcur = solid.TileVector[(size_t)ti.idx];

			// 통계
			comp.tiles.push_back({ ti.tx,ti.ty,ti.tz });
			comp.voxelCount += (Tcur.Mode == TileCPU::FULL) ? (uint64_t)TileCPU::TILE_VOXELS
				: (uint64_t)Tcur.Count;
			pushTileAABB(comp, ti.tx, ti.ty, ti.tz);

			// 6방향 이웃 검사
			for (int k = 0; k < 6; ++k) {
				int ntx = ti.tx + d6[k][0];
				int nty = ti.ty + d6[k][1];
				int ntz = ti.tz + d6[k][2];
				uint64_t nkey = pack3x21_local(ntx, nty, ntz);
				int sidx = keyToSortedIdx(nkey);
				if (sidx < 0) continue;
				int nei = rindex[sidx];
				if (visited[nei]) continue;

				const TileCPU& Tnei = solid.TileVector[(size_t)tiles[nei].idx];
				// face-overlap 체크: Tcur(facePos[k]) ∧ Tnei(faceNeg[k]) 가 1 이상인지
				if (TilesFaceConnected(Tcur, facePos[k], Tnei, faceNeg[k])) {
					visited[nei] = 1;
					q.push_back(nei);
				}
			}
		}

		// surface voxel 수 계산
		// 각 타일의 6면에 대해, "이웃 타일의 맞은편 face와 겹치지 않는 부분"만 표면으로 카운트
		uint64_t surf = 0;
		for (auto& tc : comp.tiles) {
			uint64_t faceOut[6] = { 0,0,0,0,0,0 }; // 이 타일 각 face에서 외부로 노출되는 수(최대 1024)
			const TileCPU& Tcur = solid.TileVector[(size_t)solid.findTileIndex(tc.tx, tc.ty, tc.tz)];

			for (int k = 0; k < 6; ++k) {
				int ntx = tc.tx + d6[k][0];
				int nty = tc.ty + d6[k][1];
				int ntz = tc.tz + d6[k][2];
				int nidx = solid.findTileIndex(ntx, nty, ntz);

				if (Tcur.Mode == TileCPU::FULL) {
					if (nidx < 0) { faceOut[k] = 32u * 32u; } // 외부와 접함 → 전부 표면
					else {
						const TileCPU& Tnei = solid.TileVector[(size_t)nidx];
						if (Tnei.Mode == TileCPU::FULL) {
							faceOut[k] = 0; // 내부
						}
						else {
							// 이웃이 BITSET → 이웃 face의 1의 개수만큼 내부, 나머지 표면
							FaceLayer1024 Nf; ExtractFaceLayer(Tnei, faceNeg[k], Nf);
							uint32_t nOn = Nf.popcnt();
							faceOut[k] = (uint64_t)(32u * 32u - nOn);
						}
					}
				}
				else { // BITSET
					FaceLayer1024 Cf; ExtractFaceLayer(Tcur, facePos[k], Cf);
					if (nidx < 0) {
						faceOut[k] = Cf.popcnt(); // 전부 외부로 노출
					}
					else {
						const TileCPU& Tnei = solid.TileVector[(size_t)nidx];
						if (Tnei.Mode == TileCPU::FULL) {
							faceOut[k] = 0; // 전부 내부
						}
						else {
							FaceLayer1024 Nf; ExtractFaceLayer(Tnei, faceNeg[k], Nf);
							// Cf & (~Nf) 가 표면. = Cf.popcnt() - popcnt(Cf&Nf)
							uint32_t overlap = FaceLayer1024::popcntAND(Cf, Nf);
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