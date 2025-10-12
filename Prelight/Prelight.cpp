#include "pch.h"
#include "Common/StaticMesh.h"
#include "Prelight.h"

bool ENGINECALL Prelight::Initialize()
{
	std::cout << "Prelight::Initialize called." << std::endl;
	return true;
}

void ENGINECALL Prelight::Cleanup()
{
	std::cout << "Prelight::Cleanup called." << std::endl;
}

// ================================================================
// Apove is the member method implementation of IPrelight interface.
// Underneath is the global facade implementation.
// ================================================================
// Global facade vs. member methods — why this split?
// • Member methods (above) are the *real backend implementation* of the IPrelight interface.
//   - They own/use internal state (resources, lifetime).
//   - Different backends (CPU reference, D3D12, CUDA, stub) implement these virtual methods.
//   - They live on an actual object instance (created by the app/DLL factory).
//
// • Global facade functions (below) are a thin, static-style API for callers.
//   - Callers use a single entry point: hfx::PrecomputeAtmos(...).
//   - The facade *forwards* to whichever IPrelight backend was registered via hfx::SetBackend().
//   - This keeps headers simple and hides global state: g_backend is file-local (no external symbol).
//   - It decouples the app from a specific implementation and preserves ABI: the app never calls
//     a DLL symbol directly, only the facade → easy to hot-swap backends.
//
// • Why separate them?
//   - Convenience: “static function” UX for the engine code, while still using a pluggable instance.
//   - Encapsulation: backend pointer is private to this TU; other TUs can’t see or mutate it.
//   - Flexibility: swap implementations (CPU/D3D12/CUDA) without changing call sites.
//   - Testability: register a mock IPrelight for unit tests.
//   - ODR/visibility safety: avoids inline/global variables leaking across translation units.
//
// • Lifecycle & usage:
//   - Call hfx::SetBackend(&impl) once during boot (before any facade call).
//   - Call hfx::ShutDown() during shutdown to clear the pointer.
//   - Facade asserts if no backend is set.
//
// • Threading note:
//   - Initialize/SetBackend/ShutDown should be done in a single-threaded init/shutdown phase,
//     or guarded externally if used from multiple threads.

#include "ComputeAtmos.h"

bool ENGINECALL Prelight::PrecomputeAtmos(const AtmosParams& in, AtmosResult* out) const
{
	ComputeAtmosCPU(in, out);
	return true;
}

#include "ConvexDecomposition.h"
#include "SparseBinaryGrid.h"
#include "Voxelize.h"
#include "QuickHull.h"
#include <fstream>

// ---- Corner integer key (dedup용) ----
struct CornerKey {
	int kx, ky, kz; // 격자 코너 정수좌표 (origin 기준, 칸 크기 = Cell)
	bool operator==(const CornerKey& o) const noexcept {
		return kx == o.kx && ky == o.ky && kz == o.kz;
	}
};
struct CornerKeyHash {
	size_t operator()(const CornerKey& k) const noexcept {
		// 간단 해시 (large primes)
		uint64_t h = (uint64_t)(k.kx * 73856093) ^ (uint64_t)(k.ky * 19349663) ^ (uint64_t)(k.kz * 83492791);
		return (size_t)h;
	}
};

// ---- 경계 판정: 채워져 있고, 6-이웃 중 하나라도 비어 있으면 boundary ----
static inline bool IsBoundaryVoxel(const SparseBinaryGrid& g, int x, int y, int z)
{
	ASSERT(x >= 0 && y >= 0 && z >= 0);
	if (!g.GetVoxel(x, y, z)) return false;
	static const int d6[6][3] = { {+1,0,0},{-1,0,0},{0,+1,0},{0,-1,0},{0,0,+1},{0,0,-1} };
	for (auto& d : d6) {
		if (!g.GetVoxel(x + d[0], y + d[1], z + d[2])) return true;
	}
	return false;
}

// ---- 메인: 경계 복셀들의 8 코너를 수집해 월드 좌표 반환 ----
std::vector<FLOAT3> CollectHullCornersFromSurface(const SparseBinaryGrid& grid)
{
	const int T = Brick::NUM_VOXELS_EDGE; // 타일 한 변(예: 32)
	const float h = grid.GetCellSize();          // 복셀 크기
	const FLOAT3 O = grid.GetOrigin();       // 그리드 원점

	std::unordered_set<CornerKey, CornerKeyHash> cornerSet;
	cornerSet.reserve((size_t)grid.Size() * 64); // 대충 여유 잡기

	// 타일 단위 순회
	grid.ForEachTile([&](uint64_t /*key*/, int tileIdx, int tx, int ty, int tz)
		{
			const Brick& tile = grid.GetBrick(tileIdx);
			const int baseX = tx * T;
			const int baseY = ty * T;
			const int baseZ = tz * T;

			// 간단/안전: 타일 내부 전수 검사 (T^3 = 32768) + tile.Get(li)
			// (TileCPU::FULL일 땐 모두 true)
			for (int lz = 0; lz < T; ++lz)
			{
				for (int ly = 0; ly < T; ++ly)
				{
					for (int lx = 0; lx < T; ++lx)
					{
						const int li = localIdx(lx, ly, lz);
						bool filled = (tile.Mode == Brick::FULL) ? true : tile.GetBit((uint16_t)li);
						if (!filled) continue;

						const int gx = baseX + lx;
						const int gy = baseY + ly;
						const int gz = baseZ + lz;

						// if (!IsBoundaryVoxel(grid, gx, gy, gz)) continue;

						// 경계 복셀의 8 코너 정수 좌표 (격자 코너 키: 복셀 [ix,ix+1] × [iy,iy+1] × [iz,iz+1])
						// sx,sy,sz = 0 or 1
						for (int sx = 0; sx <= 1; ++sx)
						{
							for (int sy = 0; sy <= 1; ++sy)
							{
								for (int sz = 0; sz <= 1; ++sz)
								{
									CornerKey ck{ gx + sx, gy + sy, gz + sz };
									cornerSet.insert(ck);
								}
							}
						}
					}
				}
			}
		});

	// 키 → 월드 좌표로 변환
	std::vector<FLOAT3> pts;
	pts.reserve(cornerSet.size());
	for (const CornerKey& k : cornerSet)
	{
		// world = Origin + Cell * (k)
		const float wx = O.x + h * (float)k.kx;
		const float wy = O.y + h * (float)k.ky;
		const float wz = O.z + h * (float)k.kz;
		pts.push_back({ wx, wy, wz });
	}
	return pts;
}


bool ENGINECALL Prelight::DecomposeToConvex(const StaticMesh& m) const
{
	uint numSections = (uint)m.Sections.size();
	std::vector<SparseBinaryGrid> surfaceVoxelGrid(numSections);
	std::vector<SparseBinaryGrid> solidVoxelGrid(numSections);
	std::vector<QuickHull> convexHulls(numSections);
	std::vector<std::vector<VoxelComponent>> componentsPerSection(numSections);
	size_t numTotalComponents = 0;

	for (uint section = 0; section < numSections; ++section)
	{
		uint3 gridDim = VoxelizeToSparse(
			m.Positions,
			m.Sections[section].Indices,
			m.MeshBounds,
			0.05f,
			&surfaceVoxelGrid[section]);

		std::vector<FLOAT3> hullPoints = CollectHullCornersFromSurface(surfaceVoxelGrid[section]);
		convexHulls[section] = QuickHull::Create(hullPoints);

		MakeSolidFromSurfaceSparse(
			gridDim,
			surfaceVoxelGrid[section],
			&solidVoxelGrid[section]);

		ExtractConnectedComponents6(solidVoxelGrid[section], componentsPerSection[section]);
		numTotalComponents += componentsPerSection[section].size();
	}

	// 저장

	for (const auto& surfaceGrid : surfaceVoxelGrid)
	{
		static int surfCount = 0;
		surfaceGrid.SaveAsObj(std::format("C:\\Dev\\VoxVis\\Assets\\surface_{}.obj", surfCount));
		surfCount++;
	}

	int hullCount = 0;
	for (const auto& hull : convexHulls)
	{
		hull.SaveAsOBJ(std::format("C:\\Dev\\VoxVis\\Assets\\hull_{}.obj", hullCount));
		hullCount++;
	}

	return true;
}