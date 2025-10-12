#pragma once
#include "pch.h"

class SparseBinaryGrid;

struct VoxelComponent
{
	int Id = -1;
	std::vector<uint3> BrickIndices;

	uint64_t NumVoxels = 0;
	uint64_t NumSurfaceVoxels = 0;
	
	int3 MinIdx = { +INT32_MAX, +INT32_MAX, +INT32_MAX };
	int3 MaxIdx = { -INT32_MAX, -INT32_MAX, -INT32_MAX };
};

void ExtractConnectedComponents6(
	const SparseBinaryGrid& solid,
	std::vector<VoxelComponent>& outComponents);
