#pragma once

// ------------------------------------------------
// Brick 
// ------------------------------------------------
struct Brick
{
	enum Mode : uint8_t { BITSET = 1, FULL = 2 };
	static constexpr int NUM_VOXELS_EDGE = 32;
	static constexpr int NUM_VOXELS = NUM_VOXELS_EDGE * NUM_VOXELS_EDGE * NUM_VOXELS_EDGE; // 32768
	static constexpr int NUM_WORDS_U64 = NUM_VOXELS / 64; // 512

	Mode Mode = BITSET;
	uint16_t Count = 0; // set된 복셀 수 (FULL이면 4096로 간주)
	std::array<uint64_t, NUM_WORDS_U64> Bits = {};

	inline bool GetBit(uint16_t localIndex) const;
	inline void SetBit(uint16_t localIndex, bool bState);
	inline void DemoteFullToBitsetAndClear(uint16_t localIndex);
};

// ------------------------------------------------
// Inline Method Implementation for Brick
// ------------------------------------------------

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
	if (bState && !had) { w |= m; ++Count; }
	if (!bState && had) { w &= ~m; --Count; }
}

inline void Brick::DemoteFullToBitsetAndClear(uint16_t localIndex)
{
	Mode = BITSET;
	Count = NUM_VOXELS;
	for (int i = 0; i < NUM_WORDS_U64; ++i) Bits[i] = ~0ull; // all 1
	SetBit(localIndex, false); // count--
}

// ------------------------------------------------
// SparseBinaryGrid (hash, linear probing) 
// ------------------------------------------------

class SparseBinaryGrid
{
public:
	SparseBinaryGrid() = default;
	~SparseBinaryGrid() = default;

	inline float  GetCellSize() const { return m_CellSize; }
	inline FLOAT3 GetOrigin()   const { return m_Origin; }
	inline void   Reconfigure(float cellSize, const FLOAT3& origin) { m_CellSize = cellSize; m_Origin = origin; }

	inline size_t Size() const { return m_Size; }
	inline size_t Capacity() const { return m_Capacity; }
	inline size_t GetNumBricks() const { return m_Bricks.size(); }
	inline const Brick& GetBrick(size_t index) const { return m_Bricks[index]; }

	inline void SetVoxel(int x, int y, int z, bool bState = true);
	inline bool GetVoxel(int x, int y, int z) const;

	inline void SetVoxel(uint x, uint y, uint z, bool bState = true) { SetVoxel((int)x, (int)y, (int)z, bState); }
	inline bool GetVoxel(uint x, uint y, uint z) const { return GetVoxel((int)x, (int)y, (int)z); }

	inline void SetVoxel(int3 idx, bool bState = true) { SetVoxel(idx.x, idx.y, idx.z, bState); }
	inline bool GetVoxel(int3 idx) const { return GetVoxel(idx.x, idx.y, idx.z); }
	inline void SetVoxel(uint3 idx, bool bState = true) { SetVoxel(idx.x, idx.y, idx.z, bState); }
	inline bool GetVoxel(uint3 idx) const { return GetVoxel(idx.x, idx.y, idx.z); }

	inline int FindTileIndex(int tx, int ty, int tz) const { return findTile(tx, ty, tz); }

	template<class F>
	void ForEachTile(F&& func) const;

	inline void Clear();

	bool SaveAsObj(const std::string& path) const;

private:
	static inline uint64_t hashKey(uint64_t key, uint64_t mask) { return (key * 11400714819323198485ull) & mask; } // Fibonacci hashing
	inline int findTile(int tx, int ty, int tz) const;
	inline int findOrInsertTile(int tx, int ty, int tz);

	void reserveHash(size_t want);

private:
	float m_CellSize = 1.0f;
	FLOAT3 m_Origin = { 0,0,0 };

	size_t m_Capacity = 0;
	size_t m_Size = 0;
	std::vector<uint64_t> m_Keys; // EMPTY = 0xFFFFFFFFFFFFFFFF
	std::vector<int> m_Values; // EMPTY = -1
	std::vector<Brick> m_Bricks;
};

// ------------------------ Key packing & 인덱싱 ------------------------
static inline uint64_t pack3x21(int x, int y, int z)
{
	const uint64_t B = 1ull << 20; // bias
	return ((uint64_t)((int64_t)x + (int64_t)B)) |
		((uint64_t)((int64_t)y + (int64_t)B) << 21) |
		((uint64_t)((int64_t)z + (int64_t)B) << 42);
}
static inline int localIdx(int x, int y, int z) // x-major
{
	return (x & 31) | ((y & 31) << 5) | ((z & 31) << 10);
}
static inline void indexToTile(int x, int y, int z, int& tx, int& ty, int& tz)
{
	tx = x >> 5; ty = y >> 5; tz = z >> 5;
}

// ------------------------------------------------
// Inline Method Implementation for SparseBinaryGrid
// ------------------------------------------------

inline void SparseBinaryGrid::SetVoxel(int x, int y, int z, bool bState)
{
	ASSERT(x >= 0 && y >= 0 && z >= 0);
	int tx, ty, tz; indexToTile(x, y, z, tx, ty, tz);
	int tileIdx = findOrInsertTile(tx, ty, tz);
	uint16_t li = (uint16_t)localIdx(x, y, z);

	Brick& br = m_Bricks[(size_t)tileIdx];
	if (br.Mode == Brick::FULL)
	{
		if (bState) return; // already 1
		br.DemoteFullToBitsetAndClear(li);
		return;
	}
	bool before = br.GetBit(li);
	br.SetBit(li, bState);
	if (!before && bState && br.Count == Brick::NUM_VOXELS)
	{
		br.Mode = Brick::FULL;
		br.Bits = {};
	}
}

inline bool SparseBinaryGrid::GetVoxel(int x, int y, int z) const
{
	ASSERT(x >= 0 && y >= 0 && z >= 0);
	if (x < 0 || y < 0 || z < 0) return false;
	int tx, ty, tz; indexToTile(x, y, z, tx, ty, tz);
	int tileIdx = FindTileIndex(tx, ty, tz);
	if (tileIdx < 0) return false;
	const Brick& br = m_Bricks[(size_t)tileIdx];
	if (br.Mode == Brick::FULL) return true;
	uint16_t li = (uint16_t)localIdx(x, y, z);
	return br.GetBit(li);
}

template<class F>
void SparseBinaryGrid::ForEachTile(F&& func) const
{
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

inline void SparseBinaryGrid::Clear()
{
	m_Size = 0;
	m_Bricks.clear();
	std::fill(m_Keys.begin(), m_Keys.end(), 0xFFFFFFFFFFFFFFFFull);
	std::fill(m_Values.begin(), m_Values.end(), -1);
}

inline int SparseBinaryGrid::findTile(int tx, int ty, int tz) const
{
	if (m_Capacity == 0) return -1;
	uint64_t key = pack3x21(tx, ty, tz);
	uint64_t mask = (uint64_t)(m_Capacity - 1);
	uint64_t h = hashKey(key, mask);
	for (size_t i = 0; i < m_Capacity; ++i)
	{
		if (m_Values[(size_t)h] == -1) return -1;
		if (m_Keys[(size_t)h] == key)  return m_Values[(size_t)h];
		h = (h + 1) & mask;
	}
	return -1;
}

inline int SparseBinaryGrid::findOrInsertTile(int tx, int ty, int tz)
{
	if (m_Size * 2 >= m_Capacity) reserveHash(std::max<size_t>(256, m_Capacity << 1)); // load factor <= 0.5

	uint64_t key = pack3x21(tx, ty, tz);
	uint64_t mask = (uint64_t)(m_Capacity - 1);
	uint64_t h = hashKey(key, mask);
	while (true)
	{
		if (m_Values[(size_t)h] == -1)
		{
			int idx = (int)m_Size;
			m_Bricks.push_back(Brick{}); // 새 BITSET 타일
			++m_Size;
			m_Keys[(size_t)h] = key;
			m_Values[(size_t)h] = idx;
			return idx;
		}
		if (m_Keys[(size_t)h] == key) return m_Values[(size_t)h];
		h = (h + 1) & mask;
	}
}

inline void SparseBinaryGrid::reserveHash(size_t want)
{
	if (m_Capacity >= want) return;

	size_t newCap = 1;
	while (newCap < want) newCap <<= 1;

	std::vector<uint64_t> nkeys(newCap, 0xFFFFFFFFFFFFFFFFull);
	std::vector<int>      nvals(newCap, -1);
	for (size_t i = 0; i < m_Capacity; ++i)
	{
		int v = m_Values[i];
		if (v < 0) continue;

		uint64_t k = m_Keys[i];
		uint64_t mask = (uint64_t)(newCap - 1);
		uint64_t h = hashKey(k, mask);
		while (true)
		{
			if (nvals[(size_t)h] == -1) { nkeys[(size_t)h] = k; nvals[(size_t)h] = v; break; }
			if (nkeys[(size_t)h] == k) { nvals[(size_t)h] = v; break; }
			h = (h + 1) & mask;
		}
	}
	m_Keys.swap(nkeys);
	m_Values.swap(nvals);
	m_Capacity = newCap;
}