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
		// if (x < 0 || x >= nx || y < 0 || y >= ny || z < 0 || z >= nz) return;
		ASSERT(x >= 0 && x < nx && y >= 0 && y < ny && z >= 0 && z < nz, "Voxel index out of bounds.");
		Voxels[x + y * nx + z * nx * ny] = value;
	}

	void SetVoxel(FLOAT3 pos, uint8_t value)
	{
		int ix = (int)std::floor((pos.x - Origin.x) / CellSize);
		int iy = (int)std::floor((pos.y - Origin.y) / CellSize);
		int iz = (int)std::floor((pos.z - Origin.z) / CellSize);
		SetVoxel(ix, iy, iz, value);
	}

	const uint8_t& GetVoxel(int x, int y, int z) const
	{
		return Voxels[x + y * nx + z * nx * ny];
	}
};

Grid Voxelize(const MeshData& mesh, float voxelSize);
