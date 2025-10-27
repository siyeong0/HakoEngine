#pragma once
#include "Brick.h"

// ------------------------------------------------
// SparseBinaryGrid (hash table with linear probing)
// ------------------------------------------------
// Sparse grid of 32^3 bricks addressed by brick coordinates (tx,ty,tz).
// Voxel coordinates (x,y,z) are mapped to a brick index + local index.
// Hash table stores (key=packed tx/ty/tz, value=brick index).
// Load factor maintained <= 0.5 via reserveHash (not shown here).

class SparseBinaryGrid
{
public:
	SparseBinaryGrid() = default;
	~SparseBinaryGrid() = default;

	inline float  GetCellSize() const { return m_CellSize; }
	inline FLOAT3 GetOrigin()   const { return m_Origin; }

	inline size_t Size() const { return m_Size; }          // number of occupied hash entries
	inline size_t Capacity() const { return m_Capacity; }  // hash table slots
	inline size_t GetNumBricks() const { return m_Bricks.size(); }
	inline uint3 GetDim() const { return m_Dim; }
	inline Bounds GetBounds() const;

	inline const Brick& GetBrick(size_t index) const { return m_Bricks[index]; }
	inline const Brick& GetBrick(int tx, int ty, int tz) const { return m_Bricks[findTile(tx, ty, tz)]; }
	inline const Brick& GetBrick(uint tx, uint ty, uint tz) const { return GetBrick((int)tx, (int)ty, (int)tz); }
	inline const Brick& GetBrick(int3 t)  const { return GetBrick(t.x, t.y, t.z); }
	inline const Brick& GetBrick(uint3 t) const { return GetBrick((int)t.x, (int)t.y, (int)t.z); }

	inline void SetBrick(size_t index, const Brick& src) { m_Bricks[index] = src; }
	inline void SetBrick(int tx, int ty, int tz, const Brick& src) { m_Bricks[findOrInsertTile(tx, ty, tz)] = src; }
	inline void SetBrick(uint tx, uint ty, uint tz, const Brick& src) { SetBrick((int)tx, (int)ty, (int)tz, src); }
	inline void SetBrick(int3 t, const Brick& src) { SetBrick(t.x, t.y, t.z, src); }
	inline void SetBrick(uint3 t, const Brick& src) { SetBrick((int)t.x, (int)t.y, (int)t.z, src); }

	// Local linear index inside a brick (x-major: x | (y<<5) | (z<<10))
	static inline int GetLocalIndex(int x, int y, int z); // x-major

	// Set/Get a voxel by absolute voxel coordinates.
	// Automatically creates the target brick if missing.
	inline void SetVoxel(int x, int y, int z, bool bState = true);
	inline bool GetVoxel(int x, int y, int z) const;

	// Overloads for unsigned and vector forms.
	inline void SetVoxel(uint x, uint y, uint z, bool bState = true) { SetVoxel((int)x, (int)y, (int)z, bState); }
	inline bool GetVoxel(uint x, uint y, uint z) const { return GetVoxel((int)x, (int)y, (int)z); }

	inline void SetVoxel(int3 idx, bool bState = true) { SetVoxel(idx.x, idx.y, idx.z, bState); }
	inline bool GetVoxel(int3 idx) const { return GetVoxel(idx.x, idx.y, idx.z); }
	inline void SetVoxel(uint3 idx, bool bState = true) { SetVoxel(idx.x, idx.y, idx.z, bState); }
	inline bool GetVoxel(uint3 idx) const { return GetVoxel(idx.x, idx.y, idx.z); }

	// Find a brick by brick coordinates; returns -1 if missing.
	inline int FindTileIndex(int tx, int ty, int tz) const { return findTile(tx, ty, tz); }

	// Iterate over all existing bricks; provides (key, value=brickIdx, tx,ty,tz).
	template<class F>
	void ForEachTile(F&& func) const;

	void Reconfigure(float cellSize, const FLOAT3& origin, const uint3& dim);
	size_t NumVoxels() const; // total number of set voxels in all bricks
	void Clear();
	bool SaveAsObj(const std::string& path) const;
	bool SaveAsCasper(const std::string& path) const; // CAPSER .cpr format

private:
	// Fibonacci hashing (multiplicative hashing); mask assumes power-of-two capacity.
	static inline uint64_t hashKey(uint64_t key, uint64_t mask) { return (key * 11400714819323198485ull) & mask; }
	void reserveHash(size_t want);

	// Pack three signed 21-bit values into 64 bits using a +2^20 bias each.
	// Range per axis: [-2^20, 2^20-1].
	static inline uint64_t pack3x21(int x, int y, int z);

	// Map absolute voxel index to brick coordinates (floor-div by 32 via >> 5).
	static inline void indexToTile(int x, int y, int z, int& tx, int& ty, int& tz);

	// Hash lookup: find existing bucket for (tx,ty,tz) or return -1.
	inline int findTile(int tx, int ty, int tz) const;

	// Hash insertion: insert brick if missing and return its index.
	// May trigger rehash when load factor exceeds 0.5.
	inline int findOrInsertTile(int tx, int ty, int tz);

private:
	float m_CellSize = 1.0f; // world units per voxel cell
	FLOAT3 m_Origin = { 0,0,0 }; // world-space origin (min corner) for index->position mapping
	uint3 m_Dim = { 0,0,0 }; // optional dimension

	size_t m_Capacity = 0; // number of hash slots (power of two)
	size_t m_Size = 0;     // number of occupied slots (<= m_Capacity)
	std::vector<uint64_t> m_Keys;   // EMPTY sentinel = 0xFFFFFFFFFFFFFFFF
	std::vector<int>      m_Values; // EMPTY sentinel = -1 (brick index)
	std::vector<Brick>    m_Bricks; // contiguous brick storage
};

inline Bounds SparseBinaryGrid::GetBounds() const
{
	FLOAT3 max = m_Origin + FLOAT3(m_Dim.x * m_CellSize, m_Dim.y * m_CellSize, m_Dim.z * m_CellSize);
	return Bounds(m_Origin, max);
}

inline int SparseBinaryGrid::GetLocalIndex(int x, int y, int z) // x-major
{
	// Wrap to [0,31] per axis (bitmask), then x | (y<<5) | (z<<10).
	return (x & 31) | ((y & 31) << 5) | ((z & 31) << 10);
}

inline void SparseBinaryGrid::SetVoxel(int x, int y, int z, bool bState)
{
	// Only non-negative absolute indices are allowed here.
	ASSERT(x >= 0 && y >= 0 && z >= 0);

	// Compute brick coords and ensure brick exists.
	int tx, ty, tz; indexToTile(x, y, z, tx, ty, tz);
	int tileIdx = findOrInsertTile(tx, ty, tz);
	uint16_t li = (uint16_t)GetLocalIndex(x, y, z);

	Brick& br = m_Bricks[(size_t)tileIdx];

	// If brick is FULL:
	//  - Setting a voxel ON is a no-op.
	//  - Clearing a voxel OFF requires demoting to BITSET(all 1s) then clearing one bit.
	if (br.Mode == Brick::FULL)
	{
		if (bState) return; // already 1
		br.DemoteFullToBitsetAndClear(li);
		return;
	}

	// BITSET path: toggle bit and possibly promote to FULL when Count reaches 32^3.
	bool before = br.GetBit(li);
	br.SetBit(li, bState);
	if (!before && bState && br.Count == Brick::NUM_VOXELS)
	{
		// Promote to FULL and drop the bitset storage (Bits zeroed for cleanliness).
		br.Mode = Brick::FULL;
		br.Bits = {};
	}
}

inline bool SparseBinaryGrid::GetVoxel(int x, int y, int z) const
{
	// Out-of-bounds on negative indices => empty (false).
	// Positive bounds are not checked here; caller is responsible.
	if (x < 0 || y < 0 || z < 0) return false;

	int tx, ty, tz; indexToTile(x, y, z, tx, ty, tz);
	int tileIdx = FindTileIndex(tx, ty, tz);
	if (tileIdx < 0) return false;

	const Brick& br = m_Bricks[(size_t)tileIdx];
	if (br.Mode == Brick::FULL) return true;

	uint16_t li = (uint16_t)GetLocalIndex(x, y, z);
	return br.GetBit(li);
}

template<class F>
void SparseBinaryGrid::ForEachTile(F&& func) const
{
	// Iterate the hash table and invoke func for each occupied entry.
	// Keys pack signed 21-bit (tx,ty,tz) with a +2^20 bias. Decode here.
	for (size_t i = 0; i < m_Capacity; ++i)
	{
		int v = m_Values[i];
		if (v < 0) continue;
		uint64_t key = m_Keys[i];
		int tx = (int)((int64_t)((key) & ((1ull << 21) - 1)) - (1 << 20));
		int ty = (int)((int64_t)((key >> 21) & ((1ull << 21) - 1)) - (1 << 20));
		int tz = (int)((int64_t)((key >> 42) & ((1ull << 21) - 1)) - (1 << 20));
		func(key, v, tx, ty, tz);
	}
}

inline uint64_t SparseBinaryGrid::pack3x21(int x, int y, int z)
{
	// Bias each signed coord by 2^20 so it fits into an unsigned 21-bit field.
	const uint64_t B = 1ull << 20; // bias
	return ((uint64_t)((int64_t)x + (int64_t)B)) |
		((uint64_t)((int64_t)y + (int64_t)B) << 21) |
		((uint64_t)((int64_t)z + (int64_t)B) << 42);
}

inline void SparseBinaryGrid::indexToTile(int x, int y, int z, int& tx, int& ty, int& tz)
{
	// Divide by 32 using arithmetic shift (brick edge = 32).
	tx = x >> 5; ty = y >> 5; tz = z >> 5;
}

inline int SparseBinaryGrid::findTile(int tx, int ty, int tz) const
{
	// Linear-probe lookup; stops early on first EMPTY bucket.
	if (m_Capacity == 0) return -1;
	uint64_t key = pack3x21(tx, ty, tz);
	uint64_t mask = (uint64_t)(m_Capacity - 1);
	uint64_t h = hashKey(key, mask);
	for (size_t i = 0; i < m_Capacity; ++i)
	{
		if (m_Values[(size_t)h] == -1) return -1;       // EMPTY => not present
		if (m_Keys[(size_t)h] == key)  return m_Values[(size_t)h]; // hit
		h = (h + 1) & mask; // step
	}
	return -1; // not found (table should always contain an EMPTY slot)
}

inline int SparseBinaryGrid::findOrInsertTile(int tx, int ty, int tz)
{
	// Rehash when load factor would exceed 0.5 (keeps probes short).
	if (m_Size * 2 >= m_Capacity) // load factor <= 0.5
	{
		reserveHash(std::max<size_t>(256, m_Capacity << 1));
	}

	uint64_t key = pack3x21(tx, ty, tz);
	uint64_t mask = (uint64_t)(m_Capacity - 1);
	uint64_t h = hashKey(key, mask);
	while (true)
	{
		if (m_Values[(size_t)h] == -1)
		{
			// Insert a new BITSET brick with zeroed data.
			int idx = (int)m_Size;
			m_Bricks.emplace_back(Brick{}); // new brick (defaults to BITSET)
			++m_Size;
			m_Keys[(size_t)h] = key;
			m_Values[(size_t)h] = idx;
			return idx;
		}
		if (m_Keys[(size_t)h] == key) return m_Values[(size_t)h]; // already present
		h = (h + 1) & mask;
	}
}
