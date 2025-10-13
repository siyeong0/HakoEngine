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


bool ENGINECALL Prelight::DecomposeToConvex(const StaticMesh& m) const
{
	constexpr float kVoxelSize = 0.05f;

	const uint numSections = (uint)m.Sections.size();
	std::vector<SparseBinaryGrid> surfaceGrids; 
	std::vector<std::pair<std::vector<FLOAT3>, std::vector<uint32_t>>> hulls; 
	surfaceGrids.reserve(numSections * 8);
	hulls.reserve(numSections * 8);

	for (uint section = 0; section < numSections; ++section)
	{
		// 1. Voxelize surface to sparse grid
		SparseBinaryGrid surfaceGrid;

		VoxelizeToSparse(
			m.Positions,
			m.Sections[section].Indices,
			m.MeshBounds,
			kVoxelSize,
			&surfaceGrid);

		SparseBinaryGrid solidGrid;
		MakeSolidFromSurfaceSparse(surfaceGrid, &solidGrid);

		// 2. Extract connected components
		std::vector<SparseBinaryGrid> components;
		ExtractConnectedComponents6(solidGrid, &components);
		if (components.empty())
		{
			std::cout << std::format("[Section {}] no connected components.\n", section);
			continue;
		}

		// 3. Compute convex hull over surface corners
		for (uint compIdx = 0; compIdx < components.size(); ++compIdx)
		{
			const SparseBinaryGrid& compSolid = components[compIdx];

			std::pair<std::vector<FLOAT3>, std::vector<uint32_t>> compHull;
			QuickHull(compSolid, &compHull.first, &compHull.second);

			SparseBinaryGrid compHullSurface;
			VoxelizeToSparse(
				compHull.first,
				compHull.second,
				m.MeshBounds,
				kVoxelSize,
				&compHullSurface);

			SparseBinaryGrid compHullSolid;
			MakeSolidFromSurfaceSparse(compHullSurface, &compHullSolid);

			const double concavity = ComputeConcavity(compSolid, compHullSolid);

			std::cout << std::format("Section {}: grid dim={}x{}x{}, hull dim = {}x{}x{}, concavity={}\n",
				section,
				surfaceGrid.GetDim().x, surfaceGrid.GetDim().y, surfaceGrid.GetDim().z,
				compHullSolid.GetDim().x, compHullSolid.GetDim().y, compHullSolid.GetDim().z,
				concavity);

			surfaceGrids.emplace_back(std::move(surfaceGrid));
			hulls.emplace_back(std::move(compHull));
		}
		
	}

	// 임시 저장

	for (const auto& surfaceGrid : surfaceGrids)
	{
		static int surfCount = 0;
		surfaceGrid.SaveAsObj(std::format("C:\\Dev\\VoxVis\\Assets\\surface_{}.obj", surfCount));
		surfCount++;
	}

	int hullCount = 0;
	for (const auto& hull : hulls)
	{
		SaveHullAsObj(
			std::format("C:\\Dev\\VoxVis\\Assets\\hull_{}.obj", hullCount),
			hull.first,
			hull.second);
		hullCount++;
	}

	return true;
}