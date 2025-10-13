#include "pch.h"
#include "Brick.h"
#include "FaceLayer1024.h"
#include "SparseBinaryGrid.h"

// ============================================================================
// Reconfigure
// ----------------------------------------------------------------------------
// Sets cell size, origin, and dimensions of the voxel grid.
// ===========================================================================
void SparseBinaryGrid::Reconfigure(float cellSize, const FLOAT3& origin, const uint3& dim)
{ 
    m_CellSize = cellSize;
    m_Origin = origin;
    m_Dim = dim; 
}

// ============================================================================
// NumVoxels
// ----------------------------------------------------------------------------
// Counts the total number of set voxels across all bricks.
// ============================================================================
size_t SparseBinaryGrid::NumVoxels() const
{
	size_t total = 0;
    for (const Brick& br : m_Bricks)
    {
        if (br.Mode == Brick::FULL)
        {
            total += Brick::NUM_VOXELS;
        }
        else
        {
            total += br.Count;
        }
    }
    return total;
}

// ============================================================================
// Clear
// ----------------------------------------------------------------------------
// Resets the hash table to an empty state without freeing its capacity:
// - m_Size becomes 0
// - m_Bricks storage is released (vector cleared)
// - Hash slots are marked as EMPTY (key=0xFFFFFFFFFFFFFFFF, value=-1)
// ============================================================================
void SparseBinaryGrid::Clear()
{
    m_Size = 0;
    m_Bricks.clear();
    std::fill(m_Keys.begin(), m_Keys.end(), 0xFFFFFFFFFFFFFFFFull);
    std::fill(m_Values.begin(), m_Values.end(), -1);
}

// ============================================================================
// LocalIndex3D
// ----------------------------------------------------------------------------
/*
Returns the 1D local index inside a 32×32×32 brick for (lx, ly, lz),
row-major with X fastest:
    li = (lz * 32 + ly) * 32 + lx
This helper expresses the same layout as GetLocalIndex(x,y,z) but without
bit tricks; useful for readability/tests.
*/
// ============================================================================
static inline uint16_t LocalIndex3D(int lx, int ly, int lz)
{
    return static_cast<uint16_t>((lz * Brick::NUM_VOXELS_EDGE + ly) * Brick::NUM_VOXELS_EDGE + lx);
}

// ============================================================================
// SaveAsObj
// ----------------------------------------------------------------------------
/*
Dumps the voxel grid to a Wavefront OBJ file with explicit cube geometry
for each set voxel (no instancing). Format details:

- Header includes cell size and origin for reference.
- Each voxel is emitted as:
    * 8 "v" positions (cube corners centered at voxel center, edge = cell)
    * 12 "f" triangles (CCW, outward)
- OBJ indices are 1-based; we keep a running vertex count (vcount).
- Coordinate system: cube template is CCW assuming right-handed coordinates.
- World position of voxel center:
      center = origin + ( (gx+0.5), (gy+0.5), (gz+0.5) ) * cell
- FULL bricks emit all voxels; BITSET bricks only emit set bits.

Note:
- This is a straightforward, verbose representation (potentially huge).
  For large scenes, consider surface extraction (e.g., greedy meshing or
  dual contouring) instead of per-voxel cubes.
*/
// ============================================================================
bool SparseBinaryGrid::SaveAsObj(const std::string& path) const
{
    // Open file for binary write ("wb") for portability of newlines on Windows.
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

    // Cube template (8 vertices around the center; unit cube scaled by 'cell')
    // Vertex order:
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

    // 12 triangles (3 indices each), CCW winding, outward-facing
    // Face groups: +Z (front), -Z (back), -X (left), +X (right), +Y (top), -Y (bottom)
    const int tri[12][3] = {
        {4,5,6}, {4,6,7},    // Front  (+Z)
        {1,0,3}, {1,3,2},    // Back   (-Z)
        {0,4,7}, {0,7,3},    // Left   (-X)
        {5,1,2}, {5,2,6},    // Right  (+X)
        {3,7,6}, {3,6,2},    // Top    (+Y)
        {0,1,5}, {0,5,4}     // Bottom (-Y)
    };

    // OBJ uses 1-based vertex indices. vcount tracks the total vertices written.
    size_t vcount = 0;

    // Iterate over all bricks: func(key, brickIndex, tx, ty, tz)
    this->ForEachTile([&](uint64_t /*key*/, int brickIndex, int tx, int ty, int tz)
        {
            const Brick& B = this->GetBrick((uint)brickIndex);
            constexpr int T = (int)Brick::NUM_VOXELS_EDGE;

            // Brick origin in voxel indices (global voxel coords)
            const int baseX = tx * T;
            const int baseY = ty * T;
            const int baseZ = tz * T;

            // Emits one voxel as a scaled/translated cube (8 'v' and 12 'f' records).
            auto EmitVoxel = [&](int gx, int gy, int gz)
                {
                    // Voxel center in world coordinates
                    const double cx = org.x + (double(gx) + 0.5) * double(cell);
                    const double cy = org.y + (double(gy) + 0.5) * double(cell);
                    const double cz = org.z + (double(gz) + 0.5) * double(cell);

                    // 8 vertices
                    for (int i = 0; i < 8; ++i)
                    {
                        const double px = cx + double(v8[i][0]) * double(cell);
                        const double py = cy + double(v8[i][1]) * double(cell);
                        const double pz = cz + double(v8[i][2]) * double(cell);
                        std::fprintf(f, "v %.9f %.9f %.9f\n", px, py, pz);
                    }

                    // 12 triangles (OBJ 1-based indexing)
                    const size_t base = vcount + 1;
                    for (int t = 0; t < 12; ++t) 
                    {
                        std::fprintf(f, "f %zu %zu %zu\n",
                            base + (size_t)tri[t][0],
                            base + (size_t)tri[t][1],
                            base + (size_t)tri[t][2]);
                    }
                    vcount += 8;
                };

            if (B.Mode == Brick::FULL) 
            {
                // Solid brick: emit all voxels
                for (int lz = 0; lz < T; ++lz)
                {
                    for (int ly = 0; ly < T; ++ly)
                    {
                        for (int lx = 0; lx < T; ++lx)
                        {
                            EmitVoxel(baseX + lx, baseY + ly, baseZ + lz);
                        }
                    }
                }
            }
            else 
            {
                // Bitset brick: emit only set voxels
                for (int lz = 0; lz < T; ++lz)
                {
                    for (int ly = 0; ly < T; ++ly)
                    {
                        for (int lx = 0; lx < T; ++lx)
                        {
                            // Either LocalIndex3D(lx,ly,lz) or GetLocalIndex(lx,ly,lz) work;
                            // use the grid's canonical x-major helper for consistency.
                            // const uint16_t li = LocalIndex3D(lx, ly, lz);
                            const uint16_t li = GetLocalIndex(lx, ly, lz);
                            if (!B.GetBit(li)) continue;
                            EmitVoxel(baseX + lx, baseY + ly, baseZ + lz);
                        }
                    }
                }
            }
        });

    std::fclose(f);
    return true;
}

// ============================================================================
// reserveHash
// ----------------------------------------------------------------------------
/*
Ensures the hash table has at least 'want' slots; grows to the next power of two,
re-hashes all existing entries with Fibonacci hashing, and preserves brick indices.

Notes:
- Load factor is kept <= 0.5 by callers (see findOrInsertTile).
- EMPTY sentinels: key=0xFFFFFFFFFFFFFFFF, value=-1.
- Rehash uses the same hashKey() and linear probing strategy.
*/
// ============================================================================
void SparseBinaryGrid::reserveHash(size_t want)
{
    if (m_Capacity >= want) return;

    size_t newCap = 1;
    while (newCap < want) newCap <<= 1;

    std::vector<uint64_t> nkeys(newCap, 0xFFFFFFFFFFFFFFFFull);
    std::vector<int>      nvals(newCap, -1);
    for (size_t i = 0; i < m_Capacity; ++i)
    {
        int v = m_Values[i];
        if (v < 0) continue; // skip EMPTY

        uint64_t k = m_Keys[i];
        uint64_t mask = (uint64_t)(newCap - 1);
        uint64_t h = hashKey(k, mask);
        while (true)
        {
            if (nvals[(size_t)h] == -1) { nkeys[(size_t)h] = k; nvals[(size_t)h] = v; break; }
            if (nkeys[(size_t)h] == k) { nvals[(size_t)h] = v; break; } // same key case
            h = (h + 1) & mask; // linear probe
        }
    }
    m_Keys.swap(nkeys);
    m_Values.swap(nvals);
    m_Capacity = newCap;
}
