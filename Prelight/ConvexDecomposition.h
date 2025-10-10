#pragma once
#include "pch.h"
#include "Common/Common.h"

struct Grid
{
	float CellSize;
	int nx, ny, nz;
	FLOAT3 Origin;

	std::vector<uint8_t> Voxels; // 1 byte per voxel, 0 = empty, 1 = filled

	void SetVoxel(int x, int y, int z, uint8_t value)
	{
		if (x < 0 || x >= nx || y < 0 || y >= ny || z < 0 || z >= nz) return;
		ASSERT(x >= 0 && x < nx && y >= 0 && y < ny && z >= 0 && z < nz, "Voxel index out of bounds.");
		Voxels[x + y * nx + z * nx * ny] = value;
	}

	void SetVoxel(FLOAT3 pos, uint8_t value)
	{
		int x = static_cast<int>((pos.x - Origin.x) / CellSize);
		int y = static_cast<int>((pos.y - Origin.y) / CellSize);
		int z = static_cast<int>((pos.z - Origin.z) / CellSize);
		SetVoxel(x, y, z, value);
	}

	const uint8_t& GetVoxel(int x, int y, int z) const
	{
		return Voxels[x + y * nx + z * nx * ny];
	}
};

Grid Voxelize(const MeshData& mesh, float voxelSize);
