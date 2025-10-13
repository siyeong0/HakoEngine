#pragma once

// ------------------------------------------------
// Brick
// ------------------------------------------------
// A single 32×32×32 voxel brick.
// - Mode:
//   * BITSET: occupancy stored in a 32^3 bitset (512 x uint64_t bits).
//   * FULL  : logically all voxels = 1 (Bits array is ignored/cleared).
// - Count: number of set voxels (for FULL it is effectively NUM_VOXELS = 32768).
// - Bits: packed bitset, x-major local indexing (see GetLocalIndex in grid).
struct Brick
{
	enum Mode : uint8_t { BITSET = 1, FULL = 2 };
	static constexpr int NUM_VOXELS_EDGE = 32;
	static constexpr int NUM_VOXELS = NUM_VOXELS_EDGE * NUM_VOXELS_EDGE * NUM_VOXELS_EDGE; // 32768
	static constexpr int NUM_WORDS_U64 = NUM_VOXELS / 64; // 512

	Mode Mode = BITSET;
	uint16_t Count = 0; // number of set voxels (treated as NUM_VOXELS if FULL)
	std::array<uint64_t, NUM_WORDS_U64> Bits = {};

	// Get a single local-voxel bit. Always true if FULL.
	inline bool GetBit(uint16_t localIndex) const;
	// Set/clear a single local-voxel bit and keep Count in sync.
	inline void SetBit(uint16_t localIndex, bool bState);
	// Convert FULL -> BITSET (all 1s) and then clear one bit at localIndex.
	// Used when turning off a bit in a FULL brick for the first time.
	inline void DemoteFullToBitsetAndClear(uint16_t localIndex);
};

// Implementation

inline bool Brick::GetBit(uint16_t localIndex) const
{
	if (Mode == FULL) return true;
	return (Bits[localIndex >> 6] >> (localIndex & 63)) & 1ull;
}

inline void Brick::SetBit(uint16_t localIndex, bool bState)
{
	uint64_t& w = Bits[localIndex >> 6];
	uint64_t  m = (1ull << (localIndex & 63));
	bool had = (w & m) != 0;
	// Update bit and adjust Count only on state changes.
	if (bState && !had) { w |= m; ++Count; }
	if (!bState && had) { w &= ~m; --Count; }
}

inline void Brick::DemoteFullToBitsetAndClear(uint16_t localIndex)
{
	// FULL -> BITSET with all ones, then clear one bit.
	Mode = BITSET;
	Count = NUM_VOXELS;
	for (int i = 0; i < NUM_WORDS_U64; ++i) Bits[i] = ~0ull; // all bits set to 1
	SetBit(localIndex, false); // decrement Count by clearing the requested bit
}