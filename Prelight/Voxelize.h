#pragma once

void VoxelizeToSparse(
	const std::vector<FLOAT3> vertices,
	const std::vector<uint16_t> indices,
	const Bounds& meshBounds,
	float voxelSize,
	SparseBinaryGrid* outSurfaceGrid);

void VoxelizeToSparse(
	const std::vector<FLOAT3> vertices,
	const std::vector<uint32_t> indices,
	const Bounds& meshBounds,
	float voxelSize,
	SparseBinaryGrid* outSurfaceGrid);

void MakeSolidFromSurfaceSparse(
	const SparseBinaryGrid& surface,
	SparseBinaryGrid* outSolid);