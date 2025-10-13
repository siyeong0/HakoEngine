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
#include <functional>
#include <filesystem>

// voxelHullId(gx,gy,gz) -> -1 이면 미분류(기본색), 0..H-1 이면 해당 hull 색상 사용
bool SaveAsObjColored(
	const SparseBinaryGrid& grid,
	const std::string& objPath,
	const std::function<int(int, int, int)>& voxelHullId,
	int numHulls,
	float sat = 1.f,
	float val = 1.f) // HSV의 S,V 고정
{
	namespace fs = std::filesystem;

	// 경로/MTL 설정
	const fs::path objP(objPath);
	const fs::path mtlP = objP.parent_path() / (objP.stem().string() + ".mtl");
	FILE* f = nullptr;
#if defined(_MSC_VER)
	fopen_s(&f, objP.string().c_str(), "wb");
#else
	f = std::fopen(objP.string().c_str(), "wb");
#endif
	if (!f) return false;

	// 헤더 + MTL 링크
	std::fprintf(f, "# SparseBinaryGrid OBJ export (colored by hull id)\n");
	std::fprintf(f, "mtllib %s\n", mtlP.filename().string().c_str());
	std::fprintf(f, "# cell=%.9f origin=(%.9f %.9f %.9f)\n",
		(double)grid.GetCellSize(), (double)grid.GetOrigin().x, (double)grid.GetOrigin().y, (double)grid.GetOrigin().z);

	// MTL 파일 생성
	{
		FILE* fm = nullptr;
#if defined(_MSC_VER)
		fopen_s(&fm, mtlP.string().c_str(), "wb");
#else
		fm = std::fopen(mtlP.string().c_str(), "wb");
#endif
		if (!fm) { std::fclose(f); return false; }

		// 기본(material) + 각 hull 색상
		std::fprintf(fm, "# MTL for %s\n", objP.filename().string().c_str());

		// default
		{
			std::fprintf(fm, "newmtl hull_default\n");
			std::fprintf(fm, "Kd %.6f %.6f %.6f\n", 0.8, 0.85, 0.9); // 연한 블루 톤
			std::fprintf(fm, "d 1.0f\n");
			std::fprintf(fm, "illum 2\n\n");
		}
		for (int i = 0; i < numHulls; ++i)
		{
			const float hue = std::fmodf(i * 0.61803398875f, 1.f); // 골든스텝
			float r, g, b; HSVtoRGB(hue, sat, val, r, g, b);
			std::fprintf(fm, "newmtl hull_%d\n", i);
			std::fprintf(fm, "Kd %.6f %.6f %.6f\n", r, g, b);
			std::fprintf(fm, "d 1.0f\n");
			std::fprintf(fm, "illum 2\n\n");
		}
		std::fclose(fm);
	}

	const float  cell = grid.GetCellSize();
	const FLOAT3 org = grid.GetOrigin();

	// 큐브 템플릿 (8 v / 12 tris)
	// Cube template (8 vertices around the center; unit cube scaled by 'cell')
	// Vertex order:
	//  0: (-,-,-)  1: (+,-,-)  2: (+,+,-)  3: (-,+,-)
	//  4: (-,-,+)  5: (+,-,+)  6: (+,+,+)  7: (-,+,+)
	const float v8[8][3] =
	{
		{-0.5f, -0.5f, -0.5f},
		{ 0.5f, -0.5f, -0.5f},
		{ 0.5f,  0.5f, -0.5f},
		{-0.5f,  0.5f, -0.5f},
		{-0.5f, -0.5f,  0.5f},
		{ 0.5f, -0.5f,  0.5f},
		{ 0.5f,  0.5f,  0.5f},
		{-0.5f,  0.5f,  0.5f},
	};

	// 12 triangles (3 indices each), CCW winding, outward-facing
	// Face groups: +Z (front), -Z (back), -X (left), +X (right), +Y (top), -Y (bottom)
	const int tri[12][3] =
	{
		{4,5,6}, {4,6,7},    // Front  (+Z)
		{1,0,3}, {1,3,2},    // Back   (-Z)
		{0,4,7}, {0,7,3},    // Left   (-X)
		{5,1,2}, {5,2,6},    // Right  (+X)
		{3,7,6}, {3,6,2},    // Top    (+Y)
		{0,1,5}, {0,5,4}     // Bottom (-Y)
	};

	size_t vcount = 0;
	int    currMat = -2; // 현재 적용된 material id(track용): -1=default, >=0=hull_i, -2=초기

	auto SetMaterial = [&](int matId)
		{
			if (matId == currMat) return;
			if (matId < 0) std::fprintf(f, "usemtl hull_default\n");
			else           std::fprintf(f, "usemtl hull_%d\n", matId);
			currMat = matId;
		};

	auto EmitVoxel = [&](int gx, int gy, int gz)
		{
			// 이 복셀의 material id 결정
			const int matId = voxelHullId ? voxelHullId(gx, gy, gz) : -1;
			SetMaterial(matId);

			// 중심
			const double cx = org.x + (double(gx) + 0.5) * double(cell);
			const double cy = org.y + (double(gy) + 0.5) * double(cell);
			const double cz = org.z + (double(gz) + 0.5) * double(cell);

			// 8 vertices
			for (int i = 0; i < 8; ++i)
			{
				const double px = cx + double(v8[i][0]) * double(cell);
				const double py = cy + double(v8[i][1]) * double(cell);
				const double pz = cz + double(v8[i][2]) * double(cell);
				std::fprintf(f, "v %.9f %.9f %.9f\n", px, py, pz);
			}
			const size_t base = vcount + 1;
			for (int t = 0; t < 12; ++t)
			{
				std::fprintf(f, "f %zu %zu %zu\n",
					base + (size_t)tri[t][0],
					base + (size_t)tri[t][1],
					base + (size_t)tri[t][2]);
			}
			vcount += 8;
		};

	// 전체 그리드 순회
	grid.ForEachTile([&](uint64_t /*key*/, int brickIndex, int tx, int ty, int tz)
		{
			const Brick& B = grid.GetBrick((size_t)brickIndex);
			constexpr int T = (int)Brick::NUM_VOXELS_EDGE;
			const int baseX = tx * T, baseY = ty * T, baseZ = tz * T;

			if (B.Mode == Brick::FULL)
			{
				for (int lz = 0; lz < T; ++lz)
				{
					for (int ly = 0; ly < T; ++ly)
					{
						for (int lx = 0; lx < T; ++lx)
						{
							EmitVoxel(baseX + lx, baseY + ly, baseZ + lz);
						}
					}
				}
			}
			else
			{
				for (int lz = 0; lz < T; ++lz)
				{
					for (int ly = 0; ly < T; ++ly)
					{
						for (int lx = 0; lx < T; ++lx)
						{
							const uint16_t li = SparseBinaryGrid::GetLocalIndex(lx, ly, lz);
							if (!B.GetBit(li)) continue;
							EmitVoxel(baseX + lx, baseY + ly, baseZ + lz);
						}
					}
				}
			}
		});

	std::fclose(f);
	return true;
}

bool SaveGridWithHullColors(
	const SparseBinaryGrid& baseGrid,
	const std::vector<SparseBinaryGrid>& hullSolids,
	const std::string& objPath,
	float alpha = 0.35f)
{
	auto mapper = [&](int gx, int gy, int gz)->int
		{
			for (int i = 0; i < (int)hullSolids.size(); ++i)
			{
				if (hullSolids[i].GetVoxel(gx, gy, gz))
				{
					return i; // 첫 매칭 hull 색 사용
				}
			}
			return -1;
		};
	return SaveAsObjColored(baseGrid, objPath, mapper, (int)hullSolids.size(), alpha);
}

bool ENGINECALL Prelight::DecomposeToConvex(const StaticMesh& m) const
{
	// ---- Parameters (tune) ----
	constexpr float  voxelSize = 0.05f;
	constexpr double concavityThreshold = 0.02;   // stop when part concavity <= 2%
	constexpr size_t minVoxelsPerPart = 256;    // do not split if a side gets too small
	constexpr int    maxRecursionDepth = 16;

	// Outputs
	std::vector<std::pair<std::vector<FLOAT3>, std::vector<uint32_t>>> hulls;
	std::vector<SparseBinaryGrid> leafHullSolids;
	hulls.reserve(64);
	leafHullSolids.reserve(64);

	// ---- Helpers ----

	// Build a convex hull (mesh) and its solid voxelization for a solid component grid.
	auto buildHullForSolid = [&](const SparseBinaryGrid& compSolid,
		std::pair<std::vector<FLOAT3>, std::vector<uint32_t>>& outHull,
		SparseBinaryGrid& outHullSolid)
		{
			// QuickHull overload that accepts a solid grid -> (verts, indices)
			QuickHull(compSolid, &outHull.first, &outHull.second);

			// Voxelize hull's surface back to grid space and make it solid
			SparseBinaryGrid hullSurface;
			VoxelizeToSparse(outHull.first, outHull.second, m.MeshBounds, voxelSize, &hullSurface);
			MakeSolidFromSurfaceSparse(hullSurface, &outHullSolid);
		};

	// Split a solid grid by a plane into two solid grids (A/B).
	auto splitGridByPlane = [](const SparseBinaryGrid& solid, const Plane& P)
		{
			struct { SparseBinaryGrid A, B; } part;
			part.A.Reconfigure(solid.GetCellSize(), solid.GetOrigin(), solid.GetDim());
			part.B.Reconfigure(solid.GetCellSize(), solid.GetOrigin(), solid.GetDim());

			const int T = Brick::NUM_VOXELS_EDGE;
			solid.ForEachTile([&](uint64_t /*key*/, int idx, int tx, int ty, int tz)
				{
					const Brick& br = solid.GetBrick((size_t)idx);
					const int bx = tx * T, by = ty * T, bz = tz * T;

					for (int lz = 0; lz < T; ++lz)
						for (int ly = 0; ly < T; ++ly)
							for (int lx = 0; lx < T; ++lx)
							{
								const uint16_t li = SparseBinaryGrid::GetLocalIndex(lx, ly, lz);
								if (!br.GetBit(li)) continue;

								const int gx = bx + lx, gy = by + ly, gz = bz + lz;
								const float h = solid.GetCellSize();
								const FLOAT3 o = solid.GetOrigin();
								const FLOAT3 cw{ o.x + h * (gx + 0.5f), o.y + h * (gy + 0.5f), o.z + h * (gz + 0.5f) };
								const float side = (P.n.x * cw.x + P.n.y * cw.y + P.n.z * cw.z) - P.d;

								if (side >= 0.f) part.A.SetVoxel(gx, gy, gz, true);
								else             part.B.SetVoxel(gx, gy, gz, true);
							}
				});
			return part;
		};

	// Recurse a solid component: split until concavity<=threshold or depth limit.
	std::function<void(const SparseBinaryGrid&, int)> decompose;
	decompose = [&](const SparseBinaryGrid& compSolid, int depth)
		{
			if (depth >= maxRecursionDepth) {
				// Build final hull and store
				std::pair<std::vector<FLOAT3>, std::vector<uint32_t>> H; SparseBinaryGrid Hsolid;
				buildHullForSolid(compSolid, H, Hsolid);
				hulls.emplace_back(std::move(H));
				leafHullSolids.emplace_back(std::move(Hsolid));
				return;
			}

			// Build hull and measure concavity
			std::pair<std::vector<FLOAT3>, std::vector<uint32_t>> H; SparseBinaryGrid Hsolid;
			buildHullForSolid(compSolid, H, Hsolid);

			const double c = ComputeConcavity(compSolid, Hsolid);
			if (c <= concavityThreshold) {
				hulls.emplace_back(std::move(H));
				leafHullSolids.emplace_back(std::move(Hsolid));
				return;
			}

			// Ask for a split plane (may return nullopt if no beneficial split)
			if (auto planeOpt = PickBestSplitPlane(compSolid, Hsolid, concavityThreshold, minVoxelsPerPart))
			{
				Plane P = *planeOpt;

				// Split and then re-run decomposition on each side's components
				auto parts = splitGridByPlane(compSolid, P);

				// Left/Right may contain multiple 6-connected components → extract then recurse
				std::vector<SparseBinaryGrid> leftComps;  ExtractConnectedComponents6(parts.A, &leftComps);
				std::vector<SparseBinaryGrid> rightComps; ExtractConnectedComponents6(parts.B, &rightComps);

				bool progressed = false;
				for (const auto& L : leftComps) { if (L.NumVoxels() >= minVoxelsPerPart) { decompose(L, depth + 1); progressed = true; } }
				for (const auto& R : rightComps) { if (R.NumVoxels() >= minVoxelsPerPart) { decompose(R, depth + 1); progressed = true; } }

				if (progressed) return; // successful split

				// Fallback: if split produced too small parts, accept current hull
				hulls.emplace_back(std::move(H));
				leafHullSolids.emplace_back(std::move(Hsolid));
				return;
			}

			hulls.emplace_back(std::move(H));
			leafHullSolids.emplace_back(std::move(Hsolid));
			return;
		};

	// ---- Section loop: voxelize → solidify → components → recurse ----
	for (uint section = 0; section < (uint)m.Sections.size(); ++section)
	{
		// Surface voxelization → solid fill
		SparseBinaryGrid surfaceGrid;
		VoxelizeToSparse(m.Positions, m.Sections[section].Indices, m.MeshBounds, voxelSize, &surfaceGrid);

		SparseBinaryGrid solidGrid;
		MakeSolidFromSurfaceSparse(surfaceGrid, &solidGrid);

		// 6-connected components
		std::vector<SparseBinaryGrid> components;
		ExtractConnectedComponents6(solidGrid, &components);
		if (components.empty())
		{
			std::cout << std::format("[Section {}] no connected components.\n", section);
			continue;
		}

		// Decompose each component
		for (const auto& comp : components) 
		{
			if (comp.NumVoxels() < minVoxelsPerPart) continue;
			decompose(comp, /*depth=*/0);
		}
	}

	// ---- (Optional) Save results for inspection ----
	{
		namespace fs = std::filesystem;
		const fs::path outDir = "C:\\Dev\\VoxVis\\Assets\\hulls";

		// 0) 폴더 비우기 (폴더 유지)
		{
			std::error_code ec;
			if (!fs::exists(outDir, ec)) fs::create_directories(outDir, ec);
			for (auto& e : fs::directory_iterator(outDir, ec)) fs::remove_all(e.path(), ec);
		}

		// 1) 섹션별 or 전체 저장 중 택1.
		//    여기선 "전체" 원본 solid를 컬러로 한 번 저장하고 싶다면,
		//    각 section의 solidGrid를 합치거나(별도 유틸 필요) 가장 최근 solidGrid를 사용하세요.
		//    예시로는 섹션별 저장을 보여줍니다. (surfaceGrid/solidGrid를 섹션 루프 외로 보관해두거나
		//    바로 아래에서 재계산해도 됨)

		// 섹션 루프를 다시 한 번 돌며 섹션별로 컬러 저장:
		for (uint section = 0; section < (uint)m.Sections.size(); ++section)
		{
			// (재)생성: section의 base solid grid
			SparseBinaryGrid surfaceGrid;
			VoxelizeToSparse(m.Positions, m.Sections[section].Indices, m.MeshBounds, voxelSize, &surfaceGrid);
			SparseBinaryGrid baseSolid;
			MakeSolidFromSurfaceSparse(surfaceGrid, &baseSolid);

			// 2) 매퍼: 이 복셀(gx,gy,gz)이 어떤 리프 헐에 속하는지 찾기
			auto voxelHullId = [&](int gx, int gy, int gz)->int {
				for (int i = 0; i < (int)leafHullSolids.size(); ++i) 
				{
					if (leafHullSolids[i].GetVoxel(gx, gy, gz))
					{
						return i; // 첫 매칭 hull 색
					}
				}
				return -1; // 기본색
				};

			// 3) OBJ+MTL로 저장 (HSV hue만 변경, alpha 적용)
			const std::string objPath = std::format("{}\\colored_voxels_section_{}.obj", outDir.string(), section);
			const float sat = 1.0f, val = 1.0f;

			SaveAsObjColored(baseSolid, objPath, voxelHullId, (int)leafHullSolids.size(), sat, val);
		}
	}

	return true;
}
