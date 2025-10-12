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
	uint numSections = (uint)m.Sections.size();
	std::vector<SparseBinaryGrid> surfaceVoxelGrid(numSections);
	std::vector<SparseBinaryGrid> solidVoxelGrid(numSections);
	std::vector<std::pair<std::vector<FLOAT3>, std::vector<uint32_t>>> convexHulls(numSections);
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

		std::pair<std::vector<FLOAT3>, std::vector<uint32_t>>& hull = convexHulls[section];
		QuickHull(surfaceVoxelGrid[section], &hull.first, &hull.second);

		MakeSolidFromSurfaceSparse(
			gridDim,
			surfaceVoxelGrid[section],
			&solidVoxelGrid[section]);

		//QuickHull(solidVoxelGrid[section], &hull.first, &hull.second);

		ExtractConnectedComponents6(solidVoxelGrid[section], componentsPerSection[section]);
		numTotalComponents += componentsPerSection[section].size();
	}

	// 임시 저장

	for (const auto& surfaceGrid : surfaceVoxelGrid)
	{
		static int surfCount = 0;
		surfaceGrid.SaveAsObj(std::format("C:\\Dev\\VoxVis\\Assets\\surface_{}.obj", surfCount));
		surfCount++;
	}

	int hullCount = 0;
	for (const auto& hull : convexHulls)
	{
		SaveHullAsObj(
			std::format("C:\\Dev\\VoxVis\\Assets\\hull_{}.obj", hullCount),
			hull.first,
			hull.second);
		hullCount++;
	}

	return true;
}