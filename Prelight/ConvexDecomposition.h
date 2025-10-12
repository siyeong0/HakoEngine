#pragma once
#include "pch.h"
#include "Common/Common.h"

class SparseBinaryGrid;

struct VoxelComponent
{
    int id = -1;
    // 포함되는 타일 목록 (타일 좌표)
    std::vector<uint3> tiles;

    uint64_t voxelCount = 0;   // 총 복셀 수
    uint64_t surfaceCount = 0; // 표면 복셀 수(근사 아님, face 기준 정확)
    // 복셀 인덱스 공간 AABB
    int minX = +INT32_MAX, minY = +INT32_MAX, minZ = +INT32_MAX;
    int maxX = -INT32_MAX, maxY = -INT32_MAX, maxZ = -INT32_MAX;
};

void ExtractConnectedComponents6(
    const SparseBinaryGrid& solid,
    std::vector<VoxelComponent>& outComponents);
