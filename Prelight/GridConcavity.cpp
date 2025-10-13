#include "pch.h"
#include "SparseBinaryGrid.h"
#include "ConvexDecomposition.h"

double ComputeConcavity(
    const SparseBinaryGrid& orig,
    const SparseBinaryGrid& hull)
{
	size_t numVoxelsOrig = orig.NumVoxels();
	size_t numVoxelsHull = hull.NumVoxels();
	ASSERT(numVoxelsOrig > 0 && numVoxelsHull > 0, "Both grids must contain at least one voxel");
	ASSERT(numVoxelsHull >= numVoxelsOrig, "Convex hull grid must contain at least as many voxels as the original grid");

	const double voxelSize = static_cast<double>(orig.GetCellSize());
	ASSERT(std::abs(orig.GetCellSize() - hull.GetCellSize()) < 1e-7f, "Voxel sizes of both grids must match");

	const double voxelVolume = voxelSize * voxelSize * voxelSize;
	ASSERT(voxelVolume > 1e-10, "Voxel volume is too small or invalid");

	double origVolume = voxelVolume * static_cast<double>(numVoxelsOrig);
	double hullVolume = voxelVolume * static_cast<double>(numVoxelsHull);

	return (hullVolume - origVolume) / hullVolume;
}
