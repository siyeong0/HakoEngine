#pragma once

class SparseBinaryGrid;

void QuickHull(const SparseBinaryGrid& voxelGrid, std::vector<FLOAT3>* outVertices, std::vector<uint32_t>* outIndices);
void QuickHull(const std::vector<FLOAT3>& points, std::vector<FLOAT3>* outVertices, std::vector<uint32_t>* outIndices);

bool SaveHullAsObj(const std::string& path, const std::vector<FLOAT3>& vertices, const std::vector<uint32_t>& indices);