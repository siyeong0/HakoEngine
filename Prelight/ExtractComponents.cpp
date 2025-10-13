#include "pch.h"
#include "SparseBinaryGrid.h"
#include "ConvexDecomposition.h"

// ============================================================================
// POPCOUNT64
// ----------------------------------------------------------------------------
// Platform-optimized population count for 64-bit words.
// ============================================================================

#ifndef POPCOUNT64
#  if defined(_MSC_VER)
#    include <intrin.h>
static inline uint32_t POPCOUNT64(uint64_t x) { return (uint32_t)__popcnt64(x); }
#  else
static inline uint32_t POPCOUNT64(uint64_t x) { return (uint32_t)__builtin_popcountll(x); }
#  endif
#endif

// ============================================================================
// FaceLayer1024 (32x32 = 1024 bits)
// ----------------------------------------------------------------------------
// Represents a single voxel "face slice" of a 32×32×32 brick.
// A face slice has 32×32 = 1024 cells. We pack this into 16 x uint64_t words.
// ============================================================================

struct FaceLayer1024
{
	static constexpr int WORDS = 1024 / 64; // 16
	uint64_t Words[WORDS];

	void clear() { for (int i = 0; i < WORDS; ++i) Words[i] = 0ull; }
	void setAll() { for (int i = 0; i < WORDS; ++i) Words[i] = ~0ull; }

	bool any() const
	{
		uint64_t acc = 0;
		for (int i = 0; i < WORDS; ++i)
		{
			acc |= Words[i];
		}
		return acc != 0ull;
	}

	uint32_t popcnt() const
	{
		uint32_t s = 0;
		for (int i = 0; i < WORDS; ++i)
		{
			s += POPCOUNT64(Words[i]);
		}
		return s;
	}

	static uint32_t popcntAND(const FaceLayer1024& a, const FaceLayer1024& b)
	{
		uint32_t s = 0;
		for (int i = 0; i < WORDS; ++i)
		{
			s += POPCOUNT64(a.Words[i] & b.Words[i]);
		}
		return s;
	}
};

// ============================================================================
// FACE_DIR
// ----------------------------------------------------------------------------
// Identifiers for the six axis-aligned faces of a brick.
// ============================================================================

enum FACE_DIR { NEGX = 0, POSX = 1, NEGY = 2, POSY = 3, NEGZ = 4, POSZ = 5 };

// ============================================================================
// extractFaceLayer
// ----------------------------------------------------------------------------
// Extracts a 32×32 bitfield (FaceLayer1024) from a 32×32×32 brick's chosen face.
// Indexing inside a brick uses: li = x | (y<<5) | (z<<10), with x,y,z in [0..31].
// ============================================================================

// TileCPU::Bits index: li = x | (y<<5) | (z<<10), x,y,z ∈ [0..31]
static inline void extractFaceLayer(const Brick& t, FACE_DIR f, FaceLayer1024& out)
{
	out.clear();
	if (t.Mode == Brick::FULL)
	{
		out.setAll();
		return;
	}

	if (f == NEGX || f == POSX)
	{
		const int x = (f == NEGX) ? 0 : 31;
		int bitIdx = 0;
		for (int z = 0; z < 32; ++z)
		{
			for (int y = 0; y < 32; ++y)
			{
				const uint16_t li = (uint16_t)(x | (y << 5) | (z << 10));
				const uint64_t bit = (t.Bits[li >> 6] >> (li & 63)) & 1ull;
				if (bit)
				{
					out.Words[bitIdx >> 6] |= (1ull << (bitIdx & 63));
				}
				++bitIdx;
			}
		}
		return;
	}
	if (f == NEGY || f == POSY)
	{
		const int y = (f == NEGY) ? 0 : 31;
		int bitIdx = 0;
		for (int z = 0; z < 32; ++z)
		{
			for (int x = 0; x < 32; ++x)
			{
				const uint16_t li = (uint16_t)(x | (y << 5) | (z << 10));
				const uint64_t bit = (t.Bits[li >> 6] >> (li & 63)) & 1ull;
				if (bit)
				{
					out.Words[bitIdx >> 6] |= (1ull << (bitIdx & 63));
				}
				++bitIdx;
			}
		}
		return;
	}
	{ // ZMIN / ZMAX
		const int z = (f == NEGZ) ? 0 : 31;
		int bitIdx = 0;
		for (int y = 0; y < 32; ++y)
		{
			for (int x = 0; x < 32; ++x)
			{
				const uint16_t li = (uint16_t)(x | (y << 5) | (z << 10));
				const uint64_t bit = (t.Bits[li >> 6] >> (li & 63)) & 1ull;
				if (bit)
				{
					out.Words[bitIdx >> 6] |= (1ull << (bitIdx & 63));
				}
				++bitIdx;
			}
		}
		return;
	}
}

// ============================================================================
// isTileFaceConnected
// ----------------------------------------------------------------------------
// Returns true if two bricks are "face-connected": at least one overlapping
// ============================================================================

static inline bool isTileFaceConnected(const Brick& a, FACE_DIR fa, const Brick& b, FACE_DIR fB)
{
	if (a.Mode == Brick::FULL && b.Mode == Brick::FULL) return true;
	FaceLayer1024 LA, LB;
	extractFaceLayer(a, fa, LA);
	extractFaceLayer(b, fB, LB);
	return FaceLayer1024::popcntAND(LA, LB) > 0;
}

// ============================================================================
// getTileLocalAABB
// ----------------------------------------------------------------------------
// Computes the tight local AABB of set bits inside a 32^3 brick in local
// index space [0,32). Returns false if the brick is empty (no set bits).
// ============================================================================

static inline bool getTileLocalAABB(
	const Brick& t,
	int& lx0, int& ly0, int& lz0,
	int& lx1, int& ly1, int& lz1) // [min, max) in [0,32]
{
	if (t.Mode == Brick::FULL)
	{
		lx0 = ly0 = lz0 = 0;
		lx1 = ly1 = lz1 = 32;
		return true;
	}

	// BITSET: safe-guard initial mins/maxes to detect emptiness.
	int minx = 32, miny = 32, minz = 32;
	int maxx = -1, maxy = -1, maxz = -1;

	// Bits has 512 words (= 32768 bits). Iterate only set bits per word.
	for (int wi = 0; wi < Brick::NUM_WORDS_U64; ++wi)
	{
		uint64_t w = t.Bits[(size_t)wi];
		while (w)
		{
#if defined(_MSC_VER)
			unsigned long b; _BitScanForward64(&b, w);
			int bit = (int)b;
#else
			int bit = __builtin_ctzll(w);
#endif
			const int g = (wi << 6) + bit; // 0..32767
			const int x = g & 31;
			const int y = (g >> 5) & 31;
			const int z = (g >> 10) & 31;

			if (x < minx) minx = x;
			if (y < miny) miny = y;
			if (z < minz) minz = z;
			if (x > maxx) maxx = x;
			if (y > maxy) maxy = y;
			if (z > maxz) maxz = z;

			w &= (w - 1); // clear lowest set bit
		}
	}

	if (maxx < 0) return false; // Empty brick (no set bits)

	lx0 = minx;
	ly0 = miny;
	lz0 = minz;
	lx1 = maxx + 1;
	ly1 = maxy + 1;
	lz1 = maxz + 1; // half-open upper bound
	return true;
}

// ============================================================================
// ExtractConnectedComponents6
// ----------------------------------------------------------------------------
// Computes 6-connected components over a sparse voxel grid represented by
// 32^3 bricks (sparse tiling). Two bricks belong to the same component if
// there exists a chain of neighboring bricks where each adjacent pair has at
// least one overlapping "solid" voxel on the touching faces.
// ============================================================================

void ExtractConnectedComponents6(
	const SparseBinaryGrid& solid,
	std::vector<SparseBinaryGrid>* outComponents)
{
	// (0) Early out for empty grids.
	const size_t numBricks = solid.GetNumBricks();
	ASSERT(numBricks > 0, "Empty input grid");

	// (1) Build an index → (tx,ty,tz) lookup so we can fetch tile coords in O(1).
	//     ForEachTile may iterate internal hash buckets; we fill a dense LUT.
	std::vector<uint3> coordLUT(numBricks, { (uint)INT32_MAX,(uint)INT32_MAX,(uint)INT32_MAX });
	solid.ForEachTile([&](uint64_t /*key*/, uint idx, uint tx, uint ty, uint tz) 
		{
			if ((size_t)idx < numBricks)
			{
				coordLUT[(size_t)idx] = { tx, ty, tz };
			}
		});

	// (2) Visited flags per brick index.
	std::vector<bool> bVisited(numBricks, false);

	// (3) 6-neighborhood offsets and matching face directions for the overlap test.
	const int d6[6][3] = { {+1,0,0},{-1,0,0},{0,+1,0},{0,-1,0},{0,0,+1},{0,0,-1} };
	const FACE_DIR facePos[6] = { POSX, NEGX, POSY, NEGY, POSZ, NEGZ };
	const FACE_DIR faceNeg[6] = { NEGX, POSX, NEGY, POSY, NEGZ, POSZ };

	// (4) Prepare output container.
	outComponents->clear();
	outComponents->reserve(numBricks); // worst case: each brick becomes its own component

	// (5) Run BFS from every unvisited brick; each BFS yields one component.
	for (size_t startIdx = 0; startIdx < numBricks; ++startIdx)
	{
		if (bVisited[startIdx]) continue;

		const uint3 c0 = coordLUT[startIdx];
		if (c0.x == (uint)INT32_MAX) 
		{
			// Hole in the LUT (due to hashing). Mark and skip.
			bVisited[startIdx] = true;
			continue;
		}

		// Collect brick coordinates for the current component.
		std::vector<uint3> compBricks;
		compBricks.reserve(64);

		// BFS worklist (LIFO/stack is fine for flood fill).
		std::vector<int> stack; stack.reserve(64);
		stack.emplace_back((int)startIdx);
		bVisited[startIdx] = true;

		while (!stack.empty())
		{
			const int curr = stack.back(); stack.pop_back();

			const uint3 tc = coordLUT[(size_t)curr];
			compBricks.emplace_back(tc);

			const Brick& Bcur = solid.GetBrick(curr);

			// Explore 6 neighbors.
			for (int k = 0; k < 6; ++k)
			{
				const uint ntx = tc.x + d6[k][0];
				const uint nty = tc.y + d6[k][1];
				const uint ntz = tc.z + d6[k][2];

				const int nidx = solid.FindTileIndex(ntx, nty, ntz);
				if (nidx < 0) continue;
				if (bVisited[(size_t)nidx]) continue;

				const Brick& Bnei = solid.GetBrick(nidx);

				// Face-overlap test on the touching faces.
				if (isTileFaceConnected(Bcur, facePos[k], Bnei, faceNeg[k]))
				{
					bVisited[(size_t)nidx] = true;
					stack.emplace_back(nidx);
				}
			}
		}

		// --- Construct an output grid for this component and copy its bricks ---
		// Clone meta only (cell size & origin). Hash/tiling will be rebuilt as voxels/bricks are inserted.
		SparseBinaryGrid compGrid;
		compGrid.Reconfigure(solid.GetCellSize(), solid.GetOrigin(), solid.GetDim());

		for (const uint3& tc : compBricks)
		{
			const Brick& src = solid.GetBrick(tc);
			// Insert/replace the brick at the same tile coordinates
			compGrid.SetBrick(tc, src);
		}

		outComponents->emplace_back(std::move(compGrid));
	}
}
