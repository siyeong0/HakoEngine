#pragma once
#include "pch.h"
#include "Common/Common.h"
#include <array>

// ------------------------ Key packing & 인덱싱 ------------------------
static inline uint64_t pack3x21(int x, int y, int z)
{
	const uint64_t B = 1ull << 20; // bias
	return ((uint64_t)((int64_t)x + (int64_t)B)) |
		((uint64_t)((int64_t)y + (int64_t)B) << 21) |
		((uint64_t)((int64_t)z + (int64_t)B) << 42);
}
static inline int localIdx(int x, int y, int z)
{
	return (x & 31) | ((y & 31) << 5) | ((z & 31) << 10);
}
static inline void indexToTile(int x, int y, int z, int& tx, int& ty, int& tz)
{
	tx = x >> 5; ty = y >> 5; tz = z >> 5;
}

// ------------------------ Tile (FULL / BITSET) ------------------------
struct TileCPU
{
	enum Mode : uint8_t { BITSET = 1, FULL = 2 };
	static constexpr int T = 32;
	static constexpr int TILE_VOXELS = T * T * T;          // 32768
	static constexpr int BITSET_WORDS = TILE_VOXELS / 64; // 512

	Mode Mode = BITSET;
	uint16_t Count = 0;                       // set된 복셀 수 (FULL이면 4096로 간주)
	std::array<uint64_t, BITSET_WORDS> Bits{};          // BITSET일 때만 사용

	bool Get(uint16_t localIndex) const
	{
		if (Mode == FULL) return true;
		return (Bits[localIndex >> 6] >> (localIndex & 63)) & 1ull;
	}
	void SetBitset(uint16_t localIndex, bool bState)
	{
		uint64_t& w = Bits[localIndex >> 6];
		uint64_t  m = (1ull << (localIndex & 63));
		bool had = (w & m) != 0;
		if (bState && !had) { w |= m; ++Count; }
		if (!bState && had) { w &= ~m; --Count; }
	}
	void DemoteFullToBitsetAndClear(uint16_t localIndex)
	{
		Mode = BITSET;
		Count = TILE_VOXELS;
		for (int i = 0; i < BITSET_WORDS; ++i) Bits[i] = ~0ull; // all 1
		SetBitset(localIndex, false); // count--
	}
};

// ------------------------ GPU-친화 희소 그리드(해시) ------------------------
class GpuFriendlySparseGridFB
{
public:
	// 메타 (CUDA로 넘길 때 필요)
	int T = 32;
	float Cell = 1.0f;
	FLOAT3 Origin{ 0,0,0 };

	// 해시 테이블 (pow2, 선형 프로빙)
	int Capacity = 0;
	int Size = 0;
	std::vector<uint64_t> KeyVector; // EMPTY = 0xFFFFFFFFFFFFFFFF
	std::vector<int> ValVector; // EMPTY = -1
	std::vector<TileCPU> TileVector;

	explicit GpuFriendlySparseGridFB(float cell = 1.0f, FLOAT3 origin = { 0,0,0 }, int T_ = 32)
		: T(T_)
		, Cell(cell)
		, Origin(origin)
	{
		reserveHash(256);
	}

	void Reconfigure(float cell, const FLOAT3& origin)
	{
		Cell = cell; Origin = origin;
	}

	void Clear()
	{
		Size = 0; TileVector.clear();
		std::fill(KeyVector.begin(), KeyVector.end(), 0xFFFFFFFFFFFFFFFFull);
		std::fill(ValVector.begin(), ValVector.end(), -1);
	}

	inline void SetVoxelIndex(int x, int y, int z, bool on = true)
	{
		int tx, ty, tz; indexToTile(x, y, z, tx, ty, tz);
		int tileIdx = findOrInsertTile(tx, ty, tz);
		uint16_t li = (uint16_t)localIdx(x, y, z);
		TileCPU& tile = TileVector[(size_t)tileIdx];
		if (tile.Mode == TileCPU::FULL)
		{
			if (on) return; // already 1
			tile.DemoteFullToBitsetAndClear(li);
			return;
		}
		bool before = tile.Get(li);
		tile.SetBitset(li, on);
		if (!before && on && tile.Count == TileCPU::TILE_VOXELS)
		{
			tile.Mode = TileCPU::FULL;
			tile.Bits = {};
		}
	}

	inline bool GetVoxelIndex(int x, int y, int z) const
	{
		int tx, ty, tz; indexToTile(x, y, z, tx, ty, tz);
		int tileIdx = findTile(tx, ty, tz);
		if (tileIdx < 0)
		{
			return false;
		}
		const TileCPU& tile = TileVector[(size_t)tileIdx];
		if (tile.Mode == TileCPU::FULL)
		{
			return true;
		}
		uint16_t li = (uint16_t)localIdx(x, y, z);
		return tile.Get(li);
	}

	int findTileIndex(int tx, int ty, int tz) const { return findTile(tx, ty, tz); }

	template<class F>
	void forEachTile(F&& f) const {
		for (int i = 0; i < Capacity; ++i) {
			int v = ValVector[(size_t)i];
			if (v < 0) continue;
			uint64_t key = KeyVector[(size_t)i];
			int tx = (int)((int64_t)((key) & ((1ull << 21) - 1)) - (1 << 20));
			int ty = (int)((int64_t)((key >> 21) & ((1ull << 21) - 1)) - (1 << 20));
			int tz = (int)((int64_t)((key >> 42) & ((1ull << 21) - 1)) - (1 << 20));
			f(key, v, tx, ty, tz);
		}
	}

private:
	static inline uint64_t hashKey(uint64_t key, uint64_t mask)
	{
		return (key * 11400714819323198485ull) & mask; // Fibonacci hashing
	}

	void reserveHash(int want)
	{
		if (Capacity >= want) return;

		int newCap = 1;
		while (newCap < want)
		{
			newCap <<= 1;
		}

		std::vector<uint64_t> nkeys((size_t)newCap, 0xFFFFFFFFFFFFFFFFull);
		std::vector<int>      nvals((size_t)newCap, -1);
		for (int i = 0; i < Capacity; ++i)
		{
			int v = ValVector[(size_t)i];
			if (v < 0) continue;

			uint64_t k = KeyVector[(size_t)i];
			uint64_t mask = (uint64_t)(newCap - 1);
			uint64_t h = hashKey(k, mask);
			while (true)
			{
				if (nvals[(size_t)h] == -1)
				{
					nkeys[(size_t)h] = k; nvals[(size_t)h] = v;
					break;
				}
				if (nkeys[(size_t)h] == k)
				{
					nvals[(size_t)h] = v;
					break;
				}
				h = (h + 1) & mask;
			}
		}
		KeyVector.swap(nkeys); ValVector.swap(nvals); Capacity = newCap;
	}

	int findTile(int tx, int ty, int tz) const
	{
		if (Capacity == 0) return -1;

		uint64_t key = pack3x21(tx, ty, tz);
		uint64_t mask = (uint64_t)(Capacity - 1);
		uint64_t h = hashKey(key, mask);
		for (int i = 0; i < Capacity; ++i)
		{
			if (ValVector[(size_t)h] == -1)
			{
				return -1;
			}
			if (KeyVector[(size_t)h] == key)
			{
				return ValVector[(size_t)h];
			}
			h = (h + 1) & mask;
		}
		return -1;
	}

	int findOrInsertTile(int tx, int ty, int tz)
	{
		if (Size * 2 >= Capacity)
		{
			reserveHash(std::max(256, Capacity << 1)); // load factor <= 0.5
		}

		uint64_t key = pack3x21(tx, ty, tz);
		uint64_t mask = (uint64_t)(Capacity - 1);
		uint64_t h = hashKey(key, mask);
		while (true)
		{
			if (ValVector[(size_t)h] == -1)
			{
				int idx = Size;
				TileVector.push_back(TileCPU{}); // NEW BITSET tile
				++Size;
				KeyVector[(size_t)h] = key; ValVector[(size_t)h] = idx;
				return idx;
			}
			if (KeyVector[(size_t)h] == key)
			{
				return ValVector[(size_t)h];
			}
			h = (h + 1) & mask;
		}
	}
};

struct VoxelComponent 
{
    int id = -1;
    // 포함되는 타일 목록 (타일 좌표)
    struct TileCoord { int tx, ty, tz; };
    std::vector<TileCoord> tiles;

    uint64_t voxelCount = 0;   // 총 복셀 수
    uint64_t surfaceCount = 0; // 표면 복셀 수(근사 아님, face 기준 정확)
    // 복셀 인덱스 공간 AABB
    int minX = +INT32_MAX, minY = +INT32_MAX, minZ = +INT32_MAX;
    int maxX = -INT32_MAX, maxY = -INT32_MAX, maxZ = -INT32_MAX;
};

void VoxelizeToSparse(
	const std::vector<FLOAT3> vertices,
	const std::vector<uint16_t> indices,
	const Bounds& meshBounds,
	float voxelSize,
	GpuFriendlySparseGridFB* outSolidVoxelGrid);
void ExtractConnectedComponents6(
	const GpuFriendlySparseGridFB& solid,
	std::vector<VoxelComponent>& outComponents);
