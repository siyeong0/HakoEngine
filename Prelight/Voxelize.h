#pragma once

uint3 VoxelizeToSparse(
	const std::vector<FLOAT3> vertices,
	const std::vector<uint16_t> indices,
	const Bounds& meshBounds,
	float voxelSize,
	SparseBinaryGrid* outSurfaceGrid);

void MakeSolidFromSurfaceSparse(
	uint3 dim,
	const SparseBinaryGrid& surface,
	SparseBinaryGrid* outSolid);