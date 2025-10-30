#include "pch.h"
#include <filesystem>
#include <fstream>
#include "Generic/Image.h"
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

// =============================================================
// Helper: get orthogonal projection (color + depthRG) for one face
//  - outColor   : RGBA8
//  - outDepthRG : RG(hi/lo) packed, B=0, A=255
// =============================================================
static void ProjectFace(
	const SparseBinaryGrid& grid,
	FACE_DIR face,
	Image& outColor,
	Image& outDepthRG,
	uint W, uint H)
{
	// Create blank RGBA8 targets (1 mip)
	outColor = Image::CreateBlank(W, H, Image::FORMAT_RGBA8_UNORM, 1, 1, false);
	outDepthRG = Image::CreateBlank(W, H, Image::FORMAT_RGBA8_UNORM, 1, 1, false);

	ASSERT(outColor.IsValid(), "outColor image creation failed");
	ASSERT(outDepthRG.IsValid(), "outDepthRG image creation failed");

	// Get writable pointers (cast away const for authoring)
	uint8_t* colorBase = const_cast<uint8_t*>(
		static_cast<const uint8_t*>(outColor.GetDataPtr(0, 0, 0)));
	uint8_t* depthBase = const_cast<uint8_t*>(
		static_cast<const uint8_t*>(outDepthRG.GetDataPtr(0, 0, 0)));
	const size_t colorRowPitch = outColor.GetRowPitch(0, 0, 0);
	const size_t depthRowPitch = outDepthRG.GetRowPitch(0, 0, 0);

	ASSERT(colorBase && depthBase, "Failed to map Image pixel buffers");
	ASSERT(colorRowPitch >= W * 4 && depthRowPitch >= W * 4, "RowPitch too small");

	auto toIdx4 = [&](uint x) { return static_cast<size_t>(x) * 4; };

	const uint3 dim = grid.GetDim();

	// face별 투영축/이미지 평면 축/진행방향 + 출력 플립
	int projAxis = 0;
	int axisU = 0, axisV = 1;
	int stepSign = +1;
	int startCoord = 0;
	bool flipU = false, flipV = false;

	switch (face)
	{
	case NEGX: // U=z, V=y, v뒤집기
		projAxis = 0; axisU = 2; axisV = 1; stepSign = +1; startCoord = 0;
		flipU = false; flipV = true;
		break;
	case POSX: // U=z, V=y, u/v 모두 뒤집기
		projAxis = 0; axisU = 2; axisV = 1; stepSign = -1; startCoord = (int)dim.x - 1;
		flipU = true;  flipV = true;
		break;
	case NEGY: // U=x, V=z, v뒤집기
		projAxis = 1; axisU = 0; axisV = 2; stepSign = +1; startCoord = 0;
		flipU = false; flipV = true;
		break;
	case POSY: // U=x, V=z, 뒤집기 없음
		projAxis = 1; axisU = 0; axisV = 2; stepSign = -1; startCoord = (int)dim.y - 1;
		flipU = false; flipV = false;
		break;
	case NEGZ: // U=x, V=y, u/v 모두 뒤집기
		projAxis = 2; axisU = 0; axisV = 1; stepSign = +1; startCoord = 0;
		flipU = true;  flipV = true;
		break;
	case POSZ: // U=x, V=y, v뒤집기
		projAxis = 2; axisU = 0; axisV = 1; stepSign = -1; startCoord = (int)dim.z - 1;
		flipU = false; flipV = true;
		break;
	}

	// 그리드 차원
	const int dimS = (projAxis == 0) ? (int)dim.x : (projAxis == 1) ? (int)dim.y : (int)dim.z; // 진행축 길이
	const int dimU = (axisU == 0) ? (int)dim.x : (axisU == 1) ? (int)dim.y : (int)dim.z;
	const int dimV = (axisV == 0) ? (int)dim.x : (axisV == 1) ? (int)dim.y : (int)dim.z;

	if (dimS <= 0 || dimU <= 0 || dimV <= 0) return;

	// 최근접 리샘플: (u,v 픽셀) → (gridU,gridV) 인덱스
	auto mapToGrid = [](uint p, uint P, int D) -> int {
		float t = (float(p) + 0.5f) / float(P);
		int idx = (int)floorf(t * (float)D);
		if (idx < 0) idx = 0;
		if (idx > D - 1) idx = D - 1;
		return idx;
		};

	for (uint v = 0; v < H; ++v)
	{
		const uint vv = flipV ? (H - 1 - v) : v;

		const int gridV = mapToGrid(v, H, dimV);

		uint8_t* rowC = colorBase + vv * colorRowPitch;
		uint8_t* rowD = depthBase + vv * depthRowPitch;

		for (uint u = 0; u < W; ++u)
		{
			const uint uu = flipU ? (W - 1 - u) : u;
			const int gridU = mapToGrid(u, W, dimU);

			bool hit = false;
			uint16_t depth16 = 0xFFFF;
			uint8_t cr = 255, cg = 255, cb = 255, ca = 255; // default white

			// 진행축으로 앞에서부터(혹은 뒤에서부터) 스캔
			for (int s = 0; s < dimS; ++s)
			{
				int coord[3];
				coord[projAxis] = startCoord + s * stepSign; // 진행축
				coord[axisU] = gridU;
				coord[axisV] = gridV;

				if (coord[0] < 0 || coord[1] < 0 || coord[2] < 0 ||
					coord[0] >= (int)dim.x || coord[1] >= (int)dim.y || coord[2] >= (int)dim.z)
					break;

				if (grid.GetVoxel(coord[0], coord[1], coord[2]))
				{
					hit = true;
					float depthFloat = (dimS > 1) ? (float)s / float(dimS - 1) : 0.0f;
					uint32_t d = (uint32_t)lroundf(depthFloat * 65535.0f);
					depth16 = (uint16_t)std::min(d, 65535u);
					cr = 200; cg = 200; cb = 255; ca = 255; // light-blue
					break;
				}
			}

			const size_t i4 = toIdx4(uu);

			// write color
			rowC[i4 + 0] = cr;
			rowC[i4 + 1] = cg;
			rowC[i4 + 2] = cb;
			rowC[i4 + 3] = ca;

			// write depth as RG (hi/lo), B=0, A=255
			rowD[i4 + 0] = static_cast<uint8_t>((depth16 >> 8) & 0xFF);
			rowD[i4 + 1] = static_cast<uint8_t>(depth16 & 0xFF);
			rowD[i4 + 2] = 0;
			rowD[i4 + 3] = 255;
		}
	}
}

// ============================================================================
// SaveAsCasper
// ----------------------------------------------------------------------------
bool SparseBinaryGrid::SaveAsCasper(const std::string& path) const
{
	namespace fs = std::filesystem;
	fs::path base = fs::path(path);
	std::error_code ec;
	fs::create_directories(base, ec);

	Bounds bounds = GetBounds();
	{
		fs::path metaPath = base / "metadata.txt";
		std::ofstream ofs(metaPath);
		if (!ofs)
		{
			std::cerr << "[SaveAsCasper] Failed to open metadata: " << metaPath << "\n";
			return false;
		}

		ofs << "Min" << " " << bounds.Min.x << " " << bounds.Min.y << " " << bounds.Min.z << "\n";
		ofs << "Max" << " " << bounds.Max.x << " " << bounds.Max.y << " " << bounds.Max.z << "\n";
	}

	const char* faceTag[6] = { "NX", "PX", "NY", "PY", "NZ", "PZ" };

	Image colorFacesByDir[6];
	Image depthFacesByDir[6];

	const uint cubeSize = std::max(std::max(m_Dim.x, m_Dim.y), m_Dim.z);
	for (int f = 0; f < 6; ++f)
	{
		// Create face images
		Image colorImg, depthRG;
		ProjectFace(*this, (FACE_DIR)f, colorImg, depthRG, cubeSize, cubeSize);

		// Save PNGs (color / depthRG)
		{
			fs::path colorPath = base / std::format("{}_color.png", faceTag[f]);
			bool ok = colorImg.Save(colorPath, Image::EImageFormat::PNG);
			if (!ok) std::cerr << "[SaveAsCasper] Failed to save " << colorPath << "\n";
		}
		{
			fs::path depthPath = base / std::format("{}_depth.png", faceTag[f]);
			bool ok = depthRG.Save(depthPath, Image::EImageFormat::PNG);
			if (!ok) std::cerr << "[SaveAsCasper] Failed to save " << depthPath << "\n";
		}

		colorFacesByDir[f] = std::move(colorImg);
		depthFacesByDir[f] = std::move(depthRG);
	}

	// DDS cubemap 저장 (컬러)
	// DirectX 표준 순서: +X, -X, +Y, -Y, +Z, -Z
	{
		Image cubeFaces[6];
		cubeFaces[0] = colorFacesByDir[POSX]; // +X
		cubeFaces[1] = colorFacesByDir[NEGX]; // -X
		cubeFaces[2] = colorFacesByDir[POSY]; // +Y
		cubeFaces[3] = colorFacesByDir[NEGY]; // -Y
		cubeFaces[4] = colorFacesByDir[POSZ]; // +Z
		cubeFaces[5] = colorFacesByDir[NEGZ]; // -Z
		fs::path ddsPath = base / "casper_color.dds";
		bool ok = Image::SaveToCubeMapDDS(ddsPath, cubeFaces, /*bGenerateMips*/true, /*mipLevels*/0);
		if (!ok) std::cerr << "[SaveAsCasper] Failed to save cubemap DDS: " << ddsPath << "\n";
	}

	{
		Image depthCube[6];
		depthCube[0] = depthFacesByDir[POSX];
		depthCube[1] = depthFacesByDir[NEGX];
		depthCube[2] = depthFacesByDir[POSY];
		depthCube[3] = depthFacesByDir[NEGY];
		depthCube[4] = depthFacesByDir[POSZ];
		depthCube[5] = depthFacesByDir[NEGZ];
		fs::path ddsDepthPath = base / "casper_depth.dds";
		bool ok = Image::SaveToCubeMapDDS(ddsDepthPath, depthCube, /*bGenerateMips*/false, 0);
		if (!ok) std::cerr << "[SaveAsCasper] Failed to save depth cubemap DDS: " << ddsDepthPath << "\n";
	}

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
