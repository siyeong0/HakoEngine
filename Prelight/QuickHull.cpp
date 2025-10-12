#include "pch.h"
#include "QuickHull.h"
#include "QuickHullBuilder.h"
#include "SparseBinaryGrid.h"

// ===== Public API =====
QuickHull QuickHull::Create(const std::vector<FLOAT3>& points)
{
	QuickHullBuilder qh;
	return qh.Run(points);
}

struct CornerKey 
{
	int kx, ky, kz; // 격자 코너 정수좌표 (origin 기준, 칸 크기 = Cell)
	bool operator==(const CornerKey& o) const noexcept {
		return kx == o.kx && ky == o.ky && kz == o.kz;
	}
};
struct CornerKeyHash 
{
	size_t operator()(const CornerKey& k) const noexcept 
	{
		uint64_t h = (uint64_t)(k.kx * 73856093) ^ (uint64_t)(k.ky * 19349663) ^ (uint64_t)(k.kz * 83492791);
		return (size_t)h;
	}
};

// ---- 경계 판정: 채워져 있고, 6-이웃 중 하나라도 비어 있으면 boundary ----
static inline bool isBoundaryVoxel(const SparseBinaryGrid& g, int x, int y, int z)
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
std::vector<FLOAT3> collectHullCornersFromSurface(const SparseBinaryGrid& grid)
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
						const int li = SparseBinaryGrid::GetLocalIndex(lx, ly, lz);
						bool filled = (tile.Mode == Brick::FULL) ? true : tile.GetBit((uint16_t)li);
						if (!filled) continue;

						const int gx = baseX + lx;
						const int gy = baseY + ly;
						const int gz = baseZ + lz;

						if (!isBoundaryVoxel(grid, gx, gy, gz)) continue;

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

QuickHull QuickHull::Create(const SparseBinaryGrid& grid)
{
	const std::vector<FLOAT3> conrerPoints = collectHullCornersFromSurface(grid);
	return Create(conrerPoints);
}

bool QuickHull::SaveAsOBJ(const std::string& path) const
{
	FILE* f = nullptr;
#if defined(_MSC_VER)
	fopen_s(&f, path.c_str(), "wb");
#else
	f = std::fopen(path.c_str(), "wb");
#endif
	if (!f) return false;
	std::fprintf(f, "# QuickHull OBJ export\n");
	for (const auto& v : Vertices)
	{
		std::fprintf(f, "v %.9f %.9f %.9f\n", v.x, v.y, v.z);
	}
	const size_t T = Indices.size() / 3;
	for (size_t t = 0; t < T; ++t)
	{
		int i0 = Indices[3 * t + 0] + 1, i1 = Indices[3 * t + 1] + 1, i2 = Indices[3 * t + 2] + 1;
		std::fprintf(f, "f %d %d %d\n", i0, i1, i2);
	}
	std::fclose(f);
	return true;
}
