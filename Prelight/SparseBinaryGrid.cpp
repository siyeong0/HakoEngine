#include "pch.h"
#include "SparseBinaryGrid.h"

// 복셀 로컬 인덱스(32^3)
static inline uint16_t LocalIndex3D(int lx, int ly, int lz)
{
    return static_cast<uint16_t>((lz * Brick::NUM_VOXELS_EDGE + ly) * Brick::NUM_VOXELS_EDGE + lx);
}

bool SparseBinaryGrid::SaveAsObj(const std::string& path) const
{
    // 파일 오픈
    FILE* f = nullptr;
#if defined(_MSC_VER)
    fopen_s(&f, path.c_str(), "wb");
#else
    f = std::fopen(path.c_str(), "wb");
#endif
    if (!f) return false;

    std::fprintf(f, "# SparseBinaryGrid OBJ export\n");
    std::fprintf(f, "# cell=%.9f origin=(%.9f %.9f %.9f)\n",
        (double)m_CellSize, (double)m_Origin.x, (double)m_Origin.y, (double)m_Origin.z);

    const float  cell = m_CellSize;
    const FLOAT3 org = m_Origin;

    // 큐브 8정점(센터 기준, 유니티/우핸드 기준 CCW)
    // (x,y,z) = (-1,+1) * 0.5
    //  0: (-,-,-)  1: (+,-,-)  2: (+,+,-)  3: (-,+,-)
    //  4: (-,-,+)  5: (+,-,+)  6: (+,+,+)  7: (-,+,+)
    const float v8[8][3] = {
        {-0.5f, -0.5f, -0.5f},
        { 0.5f, -0.5f, -0.5f},
        { 0.5f,  0.5f, -0.5f},
        {-0.5f,  0.5f, -0.5f},
        {-0.5f, -0.5f,  0.5f},
        { 0.5f, -0.5f,  0.5f},
        { 0.5f,  0.5f,  0.5f},
        {-0.5f,  0.5f,  0.5f},
    };

    // 12개 삼각형(각 3인덱스), CCW 바깥향
    const int tri[12][3] = {
        {4,5,6}, {4,6,7},    // Front  (+Z)
        {1,0,3}, {1,3,2},    // Back   (-Z)
        {0,4,7}, {0,7,3},    // Left   (-X)
        {5,1,2}, {5,2,6},    // Right  (+X)
        {3,7,6}, {3,6,2},    // Top    (+Y)
        {0,1,5}, {0,5,4}     // Bottom (-Y)
    };

    // OBJ는 1-based 정점 인덱스
    size_t vcount = 0;

    // 타일(브릭) 순회: func(key, brickIndex, tx, ty, tz)
    this->ForEachTile([&](uint64_t /*key*/, int brickIndex, int tx, int ty, int tz)
        {
            const Brick& B = this->GetBrick((uint)brickIndex);
            constexpr int T = (int)Brick::NUM_VOXELS_EDGE;

            // 브릭의 복셀 인덱스 기준 원점
            const int baseX = tx * T;
            const int baseY = ty * T;
            const int baseZ = tz * T;

            auto EmitVoxel = [&](int gx, int gy, int gz)
                {
                    // 복셀 중심 월드 좌표
                    const double cx = org.x + (double(gx) + 0.5) * double(cell);
                    const double cy = org.y + (double(gy) + 0.5) * double(cell);
                    const double cz = org.z + (double(gz) + 0.5) * double(cell);

                    // 8 정점
                    for (int i = 0; i < 8; ++i) {
                        const double px = cx + double(v8[i][0]) * double(cell);
                        const double py = cy + double(v8[i][1]) * double(cell);
                        const double pz = cz + double(v8[i][2]) * double(cell);
                        std::fprintf(f, "v %.9f %.9f %.9f\n", px, py, pz);
                    }

                    // 12 삼각형 (OBJ 1-based)
                    const size_t base = vcount + 1;
                    for (int t = 0; t < 12; ++t) {
                        std::fprintf(f, "f %zu %zu %zu\n",
                            base + (size_t)tri[t][0],
                            base + (size_t)tri[t][1],
                            base + (size_t)tri[t][2]);
                    }
                    vcount += 8;
                };

            if (B.Mode == Brick::FULL) {
                // 꽉찬 브릭: 전 복셀 emit
                for (int lz = 0; lz < T; ++lz)
                    for (int ly = 0; ly < T; ++ly)
                        for (int lx = 0; lx < T; ++lx) {
                            EmitVoxel(baseX + lx, baseY + ly, baseZ + lz);
                        }
            }
            else {
                // 비트셋 브릭: 켜진 복셀만 emit
                for (int lz = 0; lz < T; ++lz)
                    for (int ly = 0; ly < T; ++ly)
                        for (int lx = 0; lx < T; ++lx) {
                            // const uint16_t li = LocalIndex3D(lx, ly, lz);
                            const uint16_t li = localIdx(lx, ly, lz);
                            if (!B.GetBit(li)) continue;
                            EmitVoxel(baseX + lx, baseY + ly, baseZ + lz);
                        }
            }
        });

    std::fclose(f);
    return true;
}
