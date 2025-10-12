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
	std::vector<VoxelComponent>& outComponents)
{
	// (0) If there are no tiles, abort (defensive, also documents assumption).
	const size_t numBricks = solid.GetNumBricks();
	ASSERT(numBricks > 0, "ExtractConnectedComponents6: solid has no tiles.");

	// (1) Build tile-index -> (tx,ty,tz) lookup for O(1) coordinate fetch in BFS.
	// The container may iterate hash buckets; ForEachTile provides both tile index
	// and coordinates; fill gaps with INT32_MAX as "invalid".
	std::vector<uint3> coordLUT(numBricks, { INT32_MAX, INT32_MAX, INT32_MAX });

	// forEachTile iterates the hash slots internally, but gives (val=tileIdx) and (tx,ty,tz).
	solid.ForEachTile([&](uint64_t /*key*/, uint v, uint tx, uint ty, uint tz)
		{
			if ((size_t)v < numBricks)
			{
				coordLUT[(size_t)v] = { tx, ty, tz };
			}
		});

	// (2) Visit flags per tile index.
	std::vector<bool> bVisited(numBricks, false);

	// (3) Prepare output container.
	outComponents.clear();
	outComponents.reserve(numBricks);

	// 6-neighborhood vectors and matching face directions for overlap test:
	// facePos[k] is the face of the "current" brick toward the neighbor,
	// faceNeg[k] is the face of the neighbor toward the current brick.
	const int d6[6][3] = { {+1,0,0},{-1,0,0},{0,+1,0},{0,-1,0},{0,0,+1},{0,0,-1} };
	const FACE_DIR facePos[6] = { POSX, NEGX, POSY, NEGY, POSZ, NEGZ };
	const FACE_DIR faceNeg[6] = { NEGX, POSX, NEGY, POSY, NEGZ, POSZ };

	// (4) BFS from every unvisited tile (each BFS yields one connected component).
	for (size_t startIdx = 0; startIdx < numBricks; ++startIdx)
	{
		if (bVisited[startIdx]) continue;

		const uint3 c0 = coordLUT[startIdx];
		if (c0.x == INT32_MAX)
		{
			// Not an actual tile index (hole in LUT caused by hash structure). Skip.
			bVisited[startIdx] = true;
			continue;
		}

		VoxelComponent component;
		component.Id = (int)outComponents.size();

		// BFS worklist: store tile indices.
		std::vector<int> q; q.reserve(64);
		q.emplace_back((int)startIdx);
		bVisited[startIdx] = true;

		while (!q.empty())
		{
			const int currIdx = q.back(); q.pop_back();

			const Brick& currBrick = solid.GetBrick(currIdx);
			const uint3 tc = coordLUT[(size_t)currIdx];

			// Record brick coordinate in this component.
			component.BrickIndices.emplace_back(tc);

			// Accumulate voxel count:
			// - FULL  brick contributes 32^3
			// - BITSET brick contributes its precise Count
			component.NumVoxels += (currBrick.Mode == Brick::FULL) ? (uint64_t)Brick::NUM_VOXELS : (uint64_t)currBrick.Count;

			// Merge global AABB using local AABB translated by brick base.
			{
				int lx0, ly0, lz0, lx1, ly1, lz1;
				if (getTileLocalAABB(currBrick, lx0, ly0, lz0, lx1, ly1, lz1))
				{
					const int baseX = tc.x << 5; // brick size = 32 => <<5
					const int baseY = tc.y << 5;
					const int baseZ = tc.z << 5;

					const int gx0 = baseX + lx0;
					const int gy0 = baseY + ly0;
					const int gz0 = baseZ + lz0;

					const int gx1 = baseX + lx1;
					const int gy1 = baseY + ly1;
					const int gz1 = baseZ + lz1;

					component.MinIdx.x = std::min(component.MinIdx.x, gx0);
					component.MinIdx.y = std::min(component.MinIdx.y, gy0);
					component.MinIdx.z = std::min(component.MinIdx.z, gz0);
					component.MaxIdx.x = std::max(component.MaxIdx.x, gx1);
					component.MaxIdx.y = std::max(component.MaxIdx.y, gy1);
					component.MaxIdx.z = std::max(component.MaxIdx.z, gz1);
				}
			}

			// Explore 6 neighbors; enqueue if face-connected and not visited.
			for (int k = 0; k < 6; ++k)
			{
				const uint ntx = tc.x + d6[k][0];
				const uint nty = tc.y + d6[k][1];
				const uint ntz = tc.z + d6[k][2];

				const int neiIdx = solid.FindTileIndex(ntx, nty, ntz);
				if (neiIdx < 0) continue;
				if (bVisited[(size_t)neiIdx]) continue;

				const Brick& Tnei = solid.GetBrick(neiIdx);

				// Face-overlap check:
				// A: current brick's "positive-direction face"
				// B: neighbor brick's "negative-direction face"
				if (isTileFaceConnected(currBrick, facePos[k], Tnei, faceNeg[k]))
				{
					bVisited[(size_t)neiIdx] = true;
					q.emplace_back(neiIdx);
				}
			}
		}

		// --------------------------------------------------------------------
		// Surface voxel counting (post-BFS for this component)
		// --------------------------------------------------------------------
		// For each brick in the component and each of its six faces:
		// - Compute the number of face bits that are exposed (i.e., not overlapped
		//   by the opposite face of an adjacent brick).
		// - If neighbor doesn't exist => all current face bits are exposed.
		// - If neighbor is FULL      => no exposed bits on that side.
		// - If current is FULL:
		//     exposed = 32*32 - popcnt(neighbor opposite face)
		//
		// This yields an integer count of unit-area face cells exposed to "outside".
		// --------------------------------------------------------------------
		uint64_t surf = 0;
		for (const auto& tc : component.BrickIndices)
		{
			uint64_t faceOut[6] = { 0,0,0,0,0,0 };
			const int selfIdx = solid.FindTileIndex(tc.x, tc.y, tc.z);
			ASSERT(selfIdx >= 0);
			const Brick& Tcur = solid.GetBrick(selfIdx);

			for (int k = 0; k < 6; ++k)
			{
				const uint ntx = tc.x + d6[k][0];
				const uint nty = tc.y + d6[k][1];
				const uint ntz = tc.z + d6[k][2];
				const int nidx = solid.FindTileIndex(ntx, nty, ntz);

				if (Tcur.Mode == Brick::FULL)
				{
					if (nidx < 0)
					{
						// No neighbor => entire 32x32 face is exposed.
						faceOut[k] = 32u * 32u;
					}
					else
					{
						const Brick& Tnei = solid.GetBrick(nidx);
						if (Tnei.Mode == Brick::FULL)
						{
							// Neighbor FULL => fully occluded on this face.
							faceOut[k] = 0;
						}
						else
						{
							// FULL (current) vs BITSET (neighbor):
							// Exposed = total cells - neighbor face occupancy.
							FaceLayer1024 Nf; extractFaceLayer(Tnei, faceNeg[k], Nf);
							const uint32_t nOn = Nf.popcnt();
							faceOut[k] = (uint64_t)(32u * 32u - nOn);
						}
					}
				}
				else // current is BITSET
				{
					FaceLayer1024 Cf; extractFaceLayer(Tcur, facePos[k], Cf);
					if (nidx < 0)
					{
						// No neighbor => all current face bits are exposed.
						faceOut[k] = Cf.popcnt();
					}
					else
					{
						const Brick& Tnei = solid.GetBrick(nidx);
						if (Tnei.Mode == Brick::FULL)
						{
							// Neighbor FULL => fully occluded on this face.
							faceOut[k] = 0;
						}
						else
						{
							// BITSET vs BITSET:
							// Exposed = current face bits - overlapped bits.
							FaceLayer1024 Nf; extractFaceLayer(Tnei, faceNeg[k], Nf);
							const uint32_t overlap = FaceLayer1024::popcntAND(Cf, Nf);
							faceOut[k] = (uint64_t)Cf.popcnt() - (uint64_t)overlap;
						}
					}
				}
			}
			surf += faceOut[0] + faceOut[1] + faceOut[2] + faceOut[3] + faceOut[4] + faceOut[5];
		}
		component.NumSurfaceVoxels = surf;

		outComponents.emplace_back(std::move(component));
	}
}
