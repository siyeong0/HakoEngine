#include "pch.h"
#include "Geometry/StaticMesh.h"
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
#include <fstream>

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
			ASSERT(matId != -1);
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


//bool ENGINECALL Prelight::DecomposeToConvex(const StaticMesh& m) const
//{
//	// ---- Parameters (tune) ----
//	constexpr float  voxelSize = 0.05f;
//	constexpr double concavityThreshold = 0.4;   // stop when part concavity <= 10%
//	constexpr size_t minVoxelsPerPart = 256;    // do not split if a side gets too small
//	constexpr int    maxRecursionDepth = 16;
//
//	// Outputs
//	std::vector<std::pair<std::vector<FLOAT3>, std::vector<uint32_t>>> hulls;
//	std::vector<SparseBinaryGrid> leafHullSolids;
//	hulls.reserve(64);
//	leafHullSolids.reserve(64);
//
//	// ---- Helpers ----
//	// Compute mesh bounds
//	auto computeBounds = [](
//		const std::vector<FLOAT3>& vertices,
//		const std::vector<uint32_t>& indices) -> Bounds
//		{
//			Bounds b;
//			for (uint32_t i : indices) 
//			{
//				b.Encapsulate(vertices[i]);
//			}
//			return b;
//		};
//
//	// Build a convex hull (mesh) and its solid voxelization for a solid component grid.
//	auto buildHullForSolid = [&](const SparseBinaryGrid& compSolid,
//		std::pair<std::vector<FLOAT3>, std::vector<uint32_t>>& outHull,
//		SparseBinaryGrid& outHullSolid)
//		{
//			// QuickHull overload that accepts a solid grid -> (verts, indices)
//			QuickHull(compSolid, &outHull.first, &outHull.second);
//
//			// Voxelize hull's surface back to grid space and make it solid
//			SparseBinaryGrid hullSurface;
//			VoxelizeToSparse(
//				outHull.first, outHull.second, 
//				m.MeshBounds, // TODO: use
//				//computeBounds(outHull.first, outHull.second), -> Must reconfigure origin of the grid
//				voxelSize, &hullSurface);
//			MakeSolidFromSurfaceSparse(hullSurface, &outHullSolid);
//		};
//
//	// Split a solid grid by a plane into two solid grids (A/B).
//	auto splitGridByPlane = [](const SparseBinaryGrid& solid, const Plane& P)
//		{
//			struct { SparseBinaryGrid A, B; } part;
//			part.A.Reconfigure(solid.GetCellSize(), solid.GetOrigin(), solid.GetDim());
//			part.B.Reconfigure(solid.GetCellSize(), solid.GetOrigin(), solid.GetDim());
//
//			const int T = Brick::NUM_VOXELS_EDGE;
//			solid.ForEachTile([&](uint64_t /*key*/, int idx, int tx, int ty, int tz)
//				{
//					const Brick& br = solid.GetBrick((size_t)idx);
//					const int bx = tx * T, by = ty * T, bz = tz * T;
//
//					for (int lz = 0; lz < T; ++lz)
//						for (int ly = 0; ly < T; ++ly)
//							for (int lx = 0; lx < T; ++lx)
//							{
//								const uint16_t li = SparseBinaryGrid::GetLocalIndex(lx, ly, lz);
//								if (!br.GetBit(li)) continue;
//
//								const int gx = bx + lx, gy = by + ly, gz = bz + lz;
//								const float h = solid.GetCellSize();
//								const FLOAT3 o = solid.GetOrigin();
//								const FLOAT3 cw{ o.x + h * (gx + 0.5f), o.y + h * (gy + 0.5f), o.z + h * (gz + 0.5f) };
//								const float side = (P.n.x * cw.x + P.n.y * cw.y + P.n.z * cw.z) - P.d;
//
//								if (side >= 0.f) part.A.SetVoxel(gx, gy, gz, true);
//								else             part.B.SetVoxel(gx, gy, gz, true);
//							}
//				});
//			return part;
//		};
//
//	// Recurse a solid component: split until concavity<=threshold or depth limit.
//	std::function<void(const SparseBinaryGrid&, int)> decompose;
//	decompose = [&](const SparseBinaryGrid& compSolid, int depth)
//		{
//			if (depth >= maxRecursionDepth) {
//				// Build final hull and store
//				std::pair<std::vector<FLOAT3>, std::vector<uint32_t>> H; SparseBinaryGrid Hsolid;
//				buildHullForSolid(compSolid, H, Hsolid);
//				hulls.emplace_back(std::move(H));
//				leafHullSolids.emplace_back(std::move(Hsolid));
//				return;
//			}
//
//			// Build hull and measure concavity
//			std::pair<std::vector<FLOAT3>, std::vector<uint32_t>> H; SparseBinaryGrid Hsolid;
//			buildHullForSolid(compSolid, H, Hsolid);
//
//			// Ask for a split plane (may return nullopt if no beneficial split)
//			if (auto planeOpt = PickBestSplitPlane(compSolid, Hsolid, concavityThreshold, minVoxelsPerPart))
//			{
//				Plane P = *planeOpt;
//
//				// Split and then re-run decomposition on each side's components
//				auto parts = splitGridByPlane(compSolid, P);
//
//				// Left/Right may contain multiple 6-connected components → extract then recurse
//				std::vector<SparseBinaryGrid> leftComps;  ExtractConnectedComponents6(parts.A, &leftComps);
//				std::vector<SparseBinaryGrid> rightComps; ExtractConnectedComponents6(parts.B, &rightComps);
//
//				bool progressed = false;
//				for (const auto& L : leftComps) { if (L.NumVoxels() >= minVoxelsPerPart) { decompose(L, depth + 1); progressed = true; } }
//				for (const auto& R : rightComps) { if (R.NumVoxels() >= minVoxelsPerPart) { decompose(R, depth + 1); progressed = true; } }
//
//				if (progressed) return; // successful split
//
//				// Fallback: if split produced too small parts, accept current hull
//				hulls.emplace_back(std::move(H));
//				leafHullSolids.emplace_back(std::move(Hsolid));
//				return;
//			}
//
//			hulls.emplace_back(std::move(H));
//			leafHullSolids.emplace_back(std::move(Hsolid));
//			return;
//		};
//
//	// ---- Section loop: voxelize → solidify → components → recurse ----
//	for (uint section = 0; section < (uint)m.Sections.size(); ++section)
//	{
//		// Surface voxelization → solid fill
//		SparseBinaryGrid surfaceGrid;
//		VoxelizeToSparse(m.Positions, m.Sections[section].Indices, m.MeshBounds, voxelSize, &surfaceGrid);
//
//		SparseBinaryGrid solidGrid;
//		MakeSolidFromSurfaceSparse(surfaceGrid, &solidGrid);
//
//		if (false)
//		{
//			namespace fs = std::filesystem;
//			const fs::path outDir = "C:\\Dev\\VoxVis\\Assets\\hulls";
//			// 0) 폴더 비우기 (폴더 유지)
//			{
//				std::error_code ec;
//				if (!fs::exists(outDir, ec)) fs::create_directories(outDir, ec);
//				for (auto& e : fs::directory_iterator(outDir, ec)) fs::remove_all(e.path(), ec);
//			}
//			const fs::path metaPath = outDir / "metadata.txt";
//			std::ofstream meta(metaPath);
//			if (!meta.is_open())
//			{
//				std::cerr << "Failed to open metadata.txt for writing.\n";
//			}
//			else
//			{
//				meta << "# CASPER convex decomposition metadata\n";
//				meta << "# Each entry: leaf_index cell_size origin_x origin_y origin_z num_voxels\n";
//				meta << std::format(
//					"{}\t{:.6f}\t{:.6f}\t{:.6f}\t{:.6f}\t{}\n",
//					"leaf_",
//					solidGrid.GetCellSize(),
//					solidGrid.GetOrigin().x,
//					solidGrid.GetOrigin().y,
//					solidGrid.GetOrigin().z,
//					solidGrid.NumVoxels()
//				);
//
//				meta.close();
//				std::cout << std::format("[CASPER] Saved {} leaf grids + metadata.txt\n", leafHullSolids.size());
//			}
//			solidGrid.SaveAsCasper("C:\\Dev\\VoxVis\\Assets\\hulls\\leaf_");
//			solidGrid.SaveAsObj("C:\\Dev\\VoxVis\\Assets\\hulls\\a.obj");
//			return true;
//		}
//
//		// 6-connected components
//		std::vector<SparseBinaryGrid> components;
//		ExtractConnectedComponents6(solidGrid, &components);
//		if (components.empty())
//		{
//			std::cout << std::format("[Section {}] no connected components.\n", section);
//			continue;
//		}
//
//		// Decompose each component
//		for (const auto& comp : components) 
//		{
//			if (comp.NumVoxels() < minVoxelsPerPart) continue;
//			decompose(comp, /*depth=*/0);
//		}
//	}
//
//	// ---- (Optional) Save results for inspection ----
//	{
//		namespace fs = std::filesystem;
//		const fs::path outDir = "C:\\Dev\\VoxVis\\Assets\\hulls";
//
//		// 0) 폴더 비우기 (폴더 유지)
//		{
//			std::error_code ec;
//			if (!fs::exists(outDir, ec)) fs::create_directories(outDir, ec);
//			for (auto& e : fs::directory_iterator(outDir, ec)) fs::remove_all(e.path(), ec);
//		}
//
//		// =============================================================
//		// 📦 (NEW) Save each leaf hull as CASPER file + metadata.txt
//		// =============================================================
//		{
//			const fs::path metaPath = outDir / "metadata.txt";
//			std::ofstream meta(metaPath);
//			if (!meta.is_open())
//			{
//				std::cerr << "Failed to open metadata.txt for writing.\n";
//			}
//			else
//			{
//				meta << "# CASPER convex decomposition metadata\n";
//				meta << "# Each entry: leaf_index cell_size origin_x origin_y origin_z num_voxels\n";
//
//				for (size_t i = 0; i < leafHullSolids.size(); ++i)
//				{
//					const SparseBinaryGrid& grid = leafHullSolids[i];
//					const std::string fileName = std::format("leaf_{:04d}.cpr", (int)i);
//					const fs::path filePath = outDir / fileName;
//
//					if (grid.SaveAsCasper(filePath.string()))
//					{
//						meta << std::format(
//							"{}\t{:.6f}\t{:.6f}\t{:.6f}\t{:.6f}\t{}\n",
//							fileName,
//							grid.GetCellSize(),
//							grid.GetOrigin().x,
//							grid.GetOrigin().y,
//							grid.GetOrigin().z,
//							grid.NumVoxels()
//						);
//					}
//					else
//					{
//						meta << std::format("# FAILED {}\n", fileName);
//					}
//				}
//
//				meta.close();
//				std::cout << std::format("[CASPER] Saved {} leaf grids + metadata.txt\n", leafHullSolids.size());
//			}
//		}
//
//		// =============================================================
//		// 기존 색상 OBJ 저장 루틴
//		// =============================================================
//		for (uint section = 0; section < (uint)m.Sections.size(); ++section)
//		{
//			// (재)생성: section의 base solid grid
//			SparseBinaryGrid surfaceGrid;
//			VoxelizeToSparse(m.Positions, m.Sections[section].Indices, m.MeshBounds, voxelSize, &surfaceGrid);
//			SparseBinaryGrid baseSolid;
//			MakeSolidFromSurfaceSparse(surfaceGrid, &baseSolid);
//
//			// 매퍼: 이 복셀(gx,gy,gz)이 어떤 리프 헐에 속하는지 찾기
//			auto voxelHullId = [&](int gx, int gy, int gz)->int {
//				for (int i = 0; i < (int)leafHullSolids.size(); ++i)
//					if (leafHullSolids[i].GetVoxel(gx, gy, gz)) return i;
//				ASSERT(false);
//				return -1;
//				};
//
//			const std::string objPath = std::format("{}\\colored_voxels_section_{}.obj", outDir.string(), section);
//			const float sat = 1.0f, val = 1.0f;
//
//			SaveAsObjColored(baseSolid, objPath, voxelHullId, (int)leafHullSolids.size(), sat, val);
//		}
//
//		for (const auto& hull : hulls)
//		{
//			static int hullId = 0;
//			const fs::path hullPath = outDir / std::format("hull_{:04d}.obj", hullId++);
//			SaveHullAsObj(hullPath.string(), hull.first, hull.second);
//		}
//	}
//
//	return true;
//}

//bool ENGINECALL Prelight::DecomposeToConvex(const StaticMesh& m) const
//{
//    // ===== Params =====
//    constexpr float  kVoxelSize = 0.05f;
//    constexpr double kConcavityThreshold = 0.35;    // smaller → finer parts
//    constexpr size_t kMinVoxelsPerPart = 256;
//    constexpr int    kMaxDepth = 16;
//
//    // ===== Outputs (same format as before) =====
//    std::vector<std::pair<std::vector<FLOAT3>, std::vector<uint32_t>>> hulls;
//    std::vector<SparseBinaryGrid> leafHullSolids;
//    hulls.reserve(256);
//    leafHullSolids.reserve(256);
//
//    // ===== Helpers =====
//    auto BoundsFromGrid = [](const SparseBinaryGrid& g) -> Bounds {
//        Bounds b;
//        const FLOAT3 o = g.GetOrigin();
//        const float  h = g.GetCellSize();
//        const uint3  d = g.GetDim();
//        b.Encapsulate(o);
//        b.Encapsulate(FLOAT3{ o.x + h * d.x, o.y + h * d.y, o.z + h * d.z });
//        return b;
//        };
//
//    auto BuildHullForSolid = [&](const SparseBinaryGrid& compSolid,
//        std::pair<std::vector<FLOAT3>, std::vector<uint32_t>>& outHull,
//        SparseBinaryGrid& outHullSolid)
//        {
//            QuickHull(compSolid, &outHull.first, &outHull.second);
//            SparseBinaryGrid hullSurface;
//            VoxelizeToSparse(outHull.first, outHull.second,
//                BoundsFromGrid(compSolid),
//                compSolid.GetCellSize(), &hullSurface);
//            outHullSolid.Reconfigure(compSolid.GetCellSize(), compSolid.GetOrigin(), compSolid.GetDim());
//            MakeSolidFromSurfaceSparse(hullSurface, &outHullSolid);
//        };
//
//    auto ComputeConcavityGapSafeLocal = [&](const SparseBinaryGrid& A, const SparseBinaryGrid& H)->double
//        {
//            const size_t chCount = H.NumVoxels(); if (!chCount) return 0.0;
//            size_t gap = 0;
//            H.ForEachTile([&](uint64_t, int chIdx, int tx, int ty, int tz) {
//                const Brick& br = H.GetBrick((size_t)chIdx);
//                const int T = Brick::NUM_VOXELS_EDGE, bx = tx * T, by = ty * T, bz = tz * T;
//                for (int lz = 0; lz < T; ++lz) for (int ly = 0; ly < T; ++ly) for (int lx = 0; lx < T; ++lx) {
//                    const uint16_t li = SparseBinaryGrid::GetLocalIndex(lx, ly, lz);
//                    if (!br.GetBit(li)) continue;
//                    if (!A.GetVoxel(bx + lx, by + ly, bz + lz)) ++gap;
//                }
//                });
//            return (double)gap / (double)chCount;
//        };
//
//    struct WorkItem { SparseBinaryGrid solid; int depth = 0; double conc = 0.0; double key = 0.0; };
//    struct WorkCmp { bool operator()(const WorkItem& a, const WorkItem& b) const { return a.key < b.key; } };
//    std::priority_queue<WorkItem, std::vector<WorkItem>, WorkCmp> pq;
//
//    auto EnqueueOrFinalizeComponents =
//        [&](const SparseBinaryGrid& child, int depth, double concForWhole)
//        {
//            std::vector<SparseBinaryGrid> comps;
//            ExtractConnectedComponents6(child, &comps);
//            if (comps.empty()) return;
//
//            for (const auto& C : comps)
//            {
//                if (C.NumVoxels() < kMinVoxelsPerPart)
//                {
//                    std::pair<std::vector<FLOAT3>, std::vector<uint32_t>> Hc; SparseBinaryGrid Hsc;
//                    BuildHullForSolid(C, Hc, Hsc);
//                    hulls.emplace_back(std::move(Hc));
//                    leafHullSolids.emplace_back(std::move(Hsc));
//                    continue;
//                }
//
//                double c = concForWhole;
//                if (c < 0.0)
//                {
//                    std::pair<std::vector<FLOAT3>, std::vector<uint32_t>> H0; SparseBinaryGrid Hs0;
//                    BuildHullForSolid(C, H0, Hs0);
//                    c = std::max(0.0, ComputeConcavityGapSafeLocal(C, Hs0));
//                }
//                pq.push(WorkItem{ C, depth, c, /*key*/ c });
//            }
//        };
//
//    // ===== Section loop: voxelize → solidify → components → seed queue =====
//    std::vector<SparseBinaryGrid> allComponents; allComponents.reserve(128);
//    for (uint section = 0; section < (uint)m.Sections.size(); ++section)
//    {
//        SparseBinaryGrid surfaceGrid;
//        VoxelizeToSparse(m.Positions, m.Sections[section].Indices, m.MeshBounds, kVoxelSize, &surfaceGrid);
//
//        SparseBinaryGrid solidGrid;
//        MakeSolidFromSurfaceSparse(surfaceGrid, &solidGrid);
//
//        std::vector<SparseBinaryGrid> components;
//        ExtractConnectedComponents6(solidGrid, &components);
//        for (auto& c : components) allComponents.emplace_back(std::move(c));
//    }
//    for (const auto& comp : allComponents)
//        EnqueueOrFinalizeComponents(comp, /*depth=*/0, /*concForWhole=*/-1.0);
//
//    // ===== Split params/weights for plane picker =====
//    SplitParams sp;
//    sp.ConcavityThreshold = kConcavityThreshold;
//    sp.MinVoxelsPerPart = kMinVoxelsPerPart;
//    sp.SweepsPerAxis = 16;
//    sp.BalanceTarget = 0.5;
//    sp.EarlyRejectBalance = 0.35;
//    sp.CutBandGrid = 0.25;
//
//    SplitWeights sw; // 균형 위주. 필요 시 조정.
//
//    // ===== Best-First loop =====
//    while (!pq.empty())
//    {
//        WorkItem w = pq.top(); pq.pop();
//
//        // (A) finalize if shallow enough or depth limit
//        if (w.conc <= kConcavityThreshold || w.depth >= kMaxDepth)
//        {
//            // finalize must be single component; if not, push its components
//            std::vector<SparseBinaryGrid> chk; ExtractConnectedComponents6(w.solid, &chk);
//            if (chk.size() > 1) { for (auto& c : chk) EnqueueOrFinalizeComponents(c, w.depth, -1.0); continue; }
//
//            std::pair<std::vector<FLOAT3>, std::vector<uint32_t>> H; SparseBinaryGrid Hsolid;
//            BuildHullForSolid(w.solid, H, Hsolid);
//            hulls.emplace_back(std::move(H));
//            leafHullSolids.emplace_back(std::move(Hsolid));
//            continue;
//        }
//
//        // (B) build hull for the current part
//        std::pair<std::vector<FLOAT3>, std::vector<uint32_t>> H; SparseBinaryGrid Hsolid;
//        BuildHullForSolid(w.solid, H, Hsolid);
//
//        // (C) pick best split
//        auto best = PickBestSplitPlaneEx(w.solid, Hsolid, sp, sw);
//        if (!best.has_value())
//        {
//            // if split not beneficial, finalize (after ensuring single component)
//            std::vector<SparseBinaryGrid> chk; ExtractConnectedComponents6(w.solid, &chk);
//            if (chk.size() > 1) { for (auto& c : chk) EnqueueOrFinalizeComponents(c, w.depth, -1.0); continue; }
//
//            hulls.emplace_back(std::move(H));
//            leafHullSolids.emplace_back(std::move(Hsolid));
//            continue;
//        }
//
//        // (D) push children components (balanced best-first)
//        const SplitResult& sr = *best;
//        const double cL = std::max(0.0, sr.cA);
//        const double cR = std::max(0.0, sr.cB);
//
//        EnqueueOrFinalizeComponents(sr.A, w.depth + 1, /*concForWhole=*/cL);
//        EnqueueOrFinalizeComponents(sr.B, w.depth + 1, /*concForWhole=*/cR);
//    }
//
//    // ===== Save results (same as your original) =====
//    {
//        namespace fs = std::filesystem;
//        const fs::path outDir = "C:\\Dev\\VoxVis\\Assets\\hulls";
//
//        // 0) clear folder
//        {
//            std::error_code ec;
//            if (!fs::exists(outDir, ec)) fs::create_directories(outDir, ec);
//            for (auto& e : fs::directory_iterator(outDir, ec)) fs::remove_all(e.path(), ec);
//        }
//
//        // 1) save .cpr + metadata.txt
//        {
//            const fs::path metaPath = outDir / "metadata.txt";
//            std::ofstream meta(metaPath);
//            if (meta.is_open())
//            {
//                meta << "# CASPER convex decomposition metadata\n";
//                meta << "# Each entry: leaf_index cell_size origin_x origin_y origin_z num_voxels\n";
//                for (size_t i = 0; i < leafHullSolids.size(); ++i)
//                {
//                    const SparseBinaryGrid& grid = leafHullSolids[i];
//                    const std::string fileName = std::format("leaf_{:04d}.cpr", (int)i);
//                    const fs::path filePath = outDir / fileName;
//                    if (grid.SaveAsCasper(filePath.string()))
//                    {
//                        meta << std::format(
//                            "{}\t{:.6f}\t{:.6f}\t{:.6f}\t{:.6f}\t{}\n",
//                            fileName,
//                            grid.GetCellSize(),
//                            grid.GetOrigin().x,
//                            grid.GetOrigin().y,
//                            grid.GetOrigin().z,
//                            grid.NumVoxels());
//                    }
//                    else meta << std::format("# FAILED {}\n", fileName);
//                }
//                meta.close();
//                std::cout << std::format("[CASPER] Saved {} leaf grids + metadata.txt\n", leafHullSolids.size());
//            }
//            else {
//                std::cerr << "Failed to open metadata.txt for writing.\n";
//            }
//        }
//
//        // 2) section colored voxels
//        for (uint section = 0; section < (uint)m.Sections.size(); ++section)
//        {
//            SparseBinaryGrid surfaceGrid;
//            VoxelizeToSparse(m.Positions, m.Sections[section].Indices, m.MeshBounds, kVoxelSize, &surfaceGrid);
//            SparseBinaryGrid baseSolid;
//            MakeSolidFromSurfaceSparse(surfaceGrid, &baseSolid);
//
//            auto voxelHullId = [&](int gx, int gy, int gz)->int {
//                for (int i = 0; i < (int)leafHullSolids.size(); ++i)
//                    if (leafHullSolids[i].GetVoxel(gx, gy, gz)) return i;
//                ASSERT(false);
//                return -1;
//                };
//
//            const std::string objPath = std::format("{}\\colored_voxels_section_{}.obj", outDir.string(), section);
//            SaveAsObjColored(baseSolid, objPath, voxelHullId, (int)leafHullSolids.size(), /*sat=*/1.0f, /*val=*/1.0f);
//        }
//
//        // 3) individual hull OBJs
//        {
//            int hullId = 0;
//            for (const auto& hull : hulls)
//            {
//                const fs::path hullPath = outDir / std::format("hull_{:04d}.obj", hullId++);
//                SaveHullAsObj(hullPath.string(), hull.first, hull.second);
//            }
//        }
//    }
//
//    return true;
//}

static inline FLOAT3 VoxelCenterWS(const SparseBinaryGrid& g, int x, int y, int z)
{
    const float h = g.GetCellSize();
    const FLOAT3 o = g.GetOrigin();
    return { o.x + h * (x + 0.5f), o.y + h * (y + 0.5f), o.z + h * (z + 0.5f) };
}

// 월드좌표 p_ws가 g의 어떤 복셀에 들어있는지 안전 조회
static inline bool GetVoxelWS(const SparseBinaryGrid& g, const FLOAT3& p_ws)
{
    const float h = g.GetCellSize();
    const FLOAT3 o = g.GetOrigin();
    const uint3 dim = g.GetDim();

    // 부동소수 경계 오차 방지용 작은 여유
    constexpr float eps = 1e-5f;

    const float fx = (p_ws.x - o.x) / h;
    const float fy = (p_ws.y - o.y) / h;
    const float fz = (p_ws.z - o.z) / h;

    const int ix = (int)std::floor(fx + eps);
    const int iy = (int)std::floor(fy + eps);
    const int iz = (int)std::floor(fz + eps);

    if (ix < 0 || iy < 0 || iz < 0 || ix >= (int)dim.x || iy >= (int)dim.y || iz >= (int)dim.z) return false;
    return g.GetVoxel(ix, iy, iz);
}


#define ENABLE_VHACD_IMPLEMENTATION 1
#include "V-HACD.h"

// -----------------------------------------------------------------------------
// 유틸: 컴포넌트 그리드의 월드 AABB(복셀 파라미터 일관 유지용)
// -----------------------------------------------------------------------------
static Bounds BoundsFromGrid(const SparseBinaryGrid& g)
{
    Bounds b;
    const FLOAT3 o = g.GetOrigin();
    const float  h = g.GetCellSize();
    const uint3  d = g.GetDim();
    // 전체 그리드 경계(원하면 점유복셀 min/max로 타이트하게 변경 가능)
    b.Encapsulate(o);
    b.Encapsulate(FLOAT3{ o.x + h * d.x, o.y + h * d.y, o.z + h * d.z });
    return b;
}

// -----------------------------------------------------------------------------
// 메인: V-HACD로 볼록분해 → 동일 포맷으로 저장
// -----------------------------------------------------------------------------
bool ENGINECALL Prelight::DecomposeToConvex(const StaticMesh& m) const
{
    // ===== 파라미터 =====
    constexpr float  kVoxelSize = 0.01f;  // 출력 복셀 해상도(기존 파이프라인과 동일)

    // V-HACD 파라미터 (필요시 취향껏 조정)
    VHACD::IVHACD::Parameters par;
    par.m_maxConvexHulls = 64;        // 많으면 512
    par.m_resolution = 200000;     // 100k~400k 사이에서 시작
    par.m_minimumVolumePercentErrorAllowed = 10.0;        // 1~3%
    par.m_maxRecursionDepth = 8;         // 12~20 권장
    par.m_maxNumVerticesPerCH = 96;         // 64~128
    par.m_shrinkWrap = true;
    par.m_fillMode = VHACD::FillMode::FLOOD_FILL; // watertight mesh
    par.m_minEdgeLength = 1;          // 1~4
    par.m_asyncACD = true;
    par.m_findBestPlane = false;

    // ===== 출력 컨테이너 (기존과 동일) =====
    std::vector<std::pair<std::vector<FLOAT3>, std::vector<uint32_t>>> hulls;
    std::vector<SparseBinaryGrid> leafHullSolids;
    hulls.reserve(512);
    leafHullSolids.reserve(512);

    // ===== 헬퍼: 한 개 “단일 컴포넌트” 그리드에 대해 V-HACD 수행 =====
    auto RunVHACDOnComponent =
        [&](const SparseBinaryGrid& compGrid,
            const std::vector<FLOAT3>& positions,
            const std::vector<uint16_t>& indices)
        {
            // positions/indices 는 원본 표면 메시(섹션 단위). V-HACD는 표면 삼각형을 그대로 받음.
            if (positions.empty() || indices.size() < 3) return;

            // V-HACD 입력: double points / uint32 indices
            const size_t nPts = positions.size();
            const size_t nTris = indices.size() / 3;

            std::vector<double> pts;                    pts.resize(nPts * 3);
            std::vector<uint32_t> tris;                 tris.resize(nTris * 3);

            for (size_t i = 0; i < nPts; ++i) {
                pts[i * 3 + 0] = (double)positions[i].x;
                pts[i * 3 + 1] = (double)positions[i].y;
                pts[i * 3 + 2] = (double)positions[i].z;
            }
            for (size_t t = 0; t < nTris; ++t) {
                tris[t * 3 + 0] = (uint32_t)indices[t * 3 + 0];
                tris[t * 3 + 1] = (uint32_t)indices[t * 3 + 1];
                tris[t * 3 + 2] = (uint32_t)indices[t * 3 + 2];
            }

            // V-HACD 인스턴스 생성(동기/비동기 중 택1) — 여기선 동기를 사용
            VHACD::IVHACD* vhacd = VHACD::CreateVHACD();

            const bool ok = vhacd->Compute(
                pts.data(), (uint32_t)nPts,
                tris.data(), (uint32_t)nTris,
                par
            );
            if (!ok) {
                std::cerr << "[V-HACD] Compute failed on a component.\n";
                vhacd->Release();
                return;
            }

            const uint32_t nCH = vhacd->GetNConvexHulls();
            // std::cout << "[V-HACD] component → " << nCH << " hulls\n";

            VHACD::IVHACD::ConvexHull ch;
            for (uint32_t i = 0; i < nCH; ++i)
            {
                if (!vhacd->GetConvexHull(i, ch)) continue;

                // 1) ConvexHull → (verts, indices)
                std::pair<std::vector<FLOAT3>, std::vector<uint32_t>> H;
                H.first.resize(ch.m_points.size());
                for (size_t v = 0; v < ch.m_points.size(); ++v) {
                    const auto& p = ch.m_points[v];
                    H.first[v] = FLOAT3{ (float)p.mX, (float)p.mY, (float)p.mZ };
                }
                H.second.resize(ch.m_triangles.size() * 3);
                for (size_t t = 0; t < ch.m_triangles.size(); ++t) {
                    H.second[t * 3 + 0] = ch.m_triangles[t].mI0;
                    H.second[t * 3 + 1] = ch.m_triangles[t].mI1;
                    H.second[t * 3 + 2] = ch.m_triangles[t].mI2;
                }

                // 2) 표면 복셀화 → 솔리드화 (컴포넌트 그리드 파라미터를 그대로 사용해 일관성 유지)
                SparseBinaryGrid hullSurface;
                VoxelizeToSparse(H.first, H.second,
                    BoundsFromGrid(compGrid),
                    compGrid.GetCellSize(), &hullSurface);

                SparseBinaryGrid hullSolid;
                hullSolid.Reconfigure(compGrid.GetCellSize(), compGrid.GetOrigin(), compGrid.GetDim());
                MakeSolidFromSurfaceSparse(hullSurface, &hullSolid);

                // 3) 결과 누적(기존 포맷 동일)
                hulls.emplace_back(std::move(H));
                leafHullSolids.emplace_back(std::move(hullSolid));
            }

            vhacd->Clean();
            vhacd->Release();
        };

    // ===== 섹션 루프: 표면 복셀화 → 솔리드화 → 6-연결 컴포넌트 추출 → 각 컴포넌트에 V-HACD =====
    for (uint section = 0; section < (uint)m.Sections.size(); ++section)
    {
        // 섹션을 그리드로 만들고 …
        SparseBinaryGrid surfaceGrid;
        VoxelizeToSparse(m.Positions, m.Sections[section].Indices, m.MeshBounds, kVoxelSize, &surfaceGrid);

        SparseBinaryGrid solidGrid;
        MakeSolidFromSurfaceSparse(surfaceGrid, &solidGrid);

        // 6-연결 컴포넌트 분해(멀리 떨어진 파트가 섞여 들어가는 것 방지)
        std::vector<SparseBinaryGrid> components;
        ExtractConnectedComponents6(solidGrid, &components);

        for (const auto& comp : components)
        {
            if (comp.NumVoxels() == 0) continue;
            // 섹션의 원본 표면 메시를 그대로 넘겨도 V-HACD는 잘 동작하지만,
            // 필요하면 이 컴포넌트에 해당하는 faces만 추려서 넘기는 최적화도 가능.
            RunVHACDOnComponent(comp, m.Positions, m.Sections[section].Indices);
        }
    }

    if (leafHullSolids.empty()) {
        std::cout << "[DecomposeToConvex] V-HACD produced 0 hulls.\n";
        return true;
    }

    // ===== (Optional) 저장: 기존 포맷과 완전히 동일 =====
    {
        namespace fs = std::filesystem;
        const fs::path outDir = "C:\\Dev\\VoxVis\\Assets\\hulls";

        // 0) 폴더 비우기
        {
            std::error_code ec;
            if (!fs::exists(outDir, ec)) fs::create_directories(outDir, ec);
            for (auto& e : fs::directory_iterator(outDir, ec)) fs::remove_all(e.path(), ec);
        }

        // 1) 각 leaf hull을 CASPER(.cpr)로 저장 + metadata.txt
        {
            const fs::path metaPath = outDir / "metadata.txt";
            std::ofstream meta(metaPath);
            if (!meta.is_open())
            {
                std::cerr << "Failed to open metadata.txt for writing.\n";
            }
            else
            {
                meta << "# CASPER convex decomposition metadata\n";
                meta << "# Each entry: leaf_index cell_size origin_x origin_y origin_z num_voxels\n";

                for (size_t i = 0; i < leafHullSolids.size(); ++i)
                {
                    const SparseBinaryGrid& grid = leafHullSolids[i];
                    const std::string fileName = std::format("leaf_{:04d}.cpr", (int)i);
                    const fs::path filePath = outDir / fileName;

                    if (grid.SaveAsCasper(filePath.string()))
                    {
                        meta << std::format(
                            "{}\t{:.6f}\t{:.6f}\t{:.6f}\t{:.6f}\t{}\n",
                            fileName,
                            grid.GetCellSize(),
                            grid.GetOrigin().x,
                            grid.GetOrigin().y,
                            grid.GetOrigin().z,
                            grid.NumVoxels()
                        );
                    }
                    else
                    {
                        meta << std::format("# FAILED {}\n", fileName);
                    }
                }
                meta.close();
                std::cout << std::format("[CASPER] Saved {} leaf grids + metadata.txt\n", leafHullSolids.size());
            }
        }

        // 2) 섹션별 colored voxel OBJ (원 코드 동일)
        for (uint section = 0; section < (uint)m.Sections.size(); ++section)
        {
            SparseBinaryGrid surfaceGrid;
            VoxelizeToSparse(m.Positions, m.Sections[section].Indices, m.MeshBounds, kVoxelSize, &surfaceGrid);
            SparseBinaryGrid baseSolid;
            MakeSolidFromSurfaceSparse(surfaceGrid, &baseSolid);

            std::vector<Bounds> hullWSAABBs;
            hullWSAABBs.reserve(leafHullSolids.size());
            for (const auto& g : leafHullSolids) {
                Bounds b;
                const FLOAT3 o = g.GetOrigin();
                const uint3 d = g.GetDim();
                const float h = g.GetCellSize();
                b.Encapsulate(o);
                b.Encapsulate(FLOAT3{ o.x + h * d.x, o.y + h * d.y, o.z + h * d.z });
                hullWSAABBs.emplace_back(b);
            }

            auto voxelHullId = [&](int gx, int gy, int gz)->int {
                // baseSolid(섹션 그리드) 복셀의 월드 중심
                const FLOAT3 cws = VoxelCenterWS(baseSolid, gx, gy, gz);

                // 후보 헐만 검사(월드 AABB로 1차 필터)
                for (int i = 0; i < (int)leafHullSolids.size(); ++i)
                {
                    const Bounds& wb = hullWSAABBs[i];
                    if (!wb.Contains(cws)) continue;            // AABB 밖이면 스킵
                    if (GetVoxelWS(leafHullSolids[i], cws))     // 월드→각 헐 그리드로 변환해 점유 확인
                        return i;
                }

                // 여기까지 못 찾으면(희귀), 가장 가까운 AABB를 가진 헐로 폴백(누락 방지)
                int    best = -1;
                float  bestDist2 = FLT_MAX;
                for (int i = 0; i < (int)leafHullSolids.size(); ++i)
                {
                    const Bounds& wb = hullWSAABBs[i];
                    // AABB에 clamp한 최근접점까지의 거리
                    const FLOAT3 q{
                        std::min(std::max(cws.x, wb.Min.x), wb.Max.x),
                        std::min(std::max(cws.y, wb.Min.y), wb.Max.y),
                        std::min(std::max(cws.z, wb.Min.z), wb.Max.z)
                    };
                    const FLOAT3 d = cws - q;
                    const float d2 = d.Dot(d);
                    if (d2 < bestDist2) { bestDist2 = d2; best = i; }
                }
                return (best >= 0 ? best : 0); // 최소한 색은 입히도록 0으로 폴백 가능
                };

            const std::string objPath = std::format("{}\\colored_voxels_section_{}.obj", outDir.string(), section);
            const float sat = 1.0f, val = 1.0f;
            SaveAsObjColored(baseSolid, objPath, voxelHullId, (int)leafHullSolids.size(), sat, val);
        }

        // 3) 각 볼록 헐을 OBJ로 저장 (원 코드 동일)
        {
            int hullId = 0;
            for (const auto& hull : hulls)
            {
                const fs::path hullPath = outDir / std::format("hull_{:04d}.obj", hullId++);
                SaveHullAsObj(hullPath.string(), hull.first, hull.second);
            }
        }
    }

    return true;
}