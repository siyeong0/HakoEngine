#pragma once
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
// FACE_DIR
// ----------------------------------------------------------------------------
// Identifiers for the six axis-aligned faces of a brick.
enum FACE_DIR { NEGX = 0, POSX = 1, NEGY = 2, POSY = 3, NEGZ = 4, POSZ = 5 };

// ============================================================================
// FaceLayer1024 (32x32 = 1024 bits)
// ----------------------------------------------------------------------------
// Represents a single voxel "face slice" of a 32×32×32 brick.
// A face slice has 32×32 = 1024 cells. We pack this into 16 x uint64_t words.
struct FaceLayer1024
{
	static constexpr int WORDS = 1024 / 64; // 16
	uint64_t Words[WORDS];

	void Clear() { for (int i = 0; i < WORDS; ++i) Words[i] = 0ull; }
	void SetAll() { for (int i = 0; i < WORDS; ++i) Words[i] = ~0ull; }

	bool Any() const
	{
		uint64_t acc = 0;
		for (int i = 0; i < WORDS; ++i)
		{
			acc |= Words[i];
		}
		return acc != 0ull;
	}

	bool IsZero() const
	{
		for (int i = 0; i < WORDS; ++i) if (Words[i]) return false;
		return true;
	}

	uint32_t PopCount() const
	{
		uint32_t s = 0;
		for (int i = 0; i < WORDS; ++i)
		{
			s += POPCOUNT64(Words[i]);
		}
		return s;
	}

	static uint32_t PopCountAND(const FaceLayer1024& a, const FaceLayer1024& b) {

		uint32_t s = 0;
		for (int i = 0; i < WORDS; ++i)
		{
			s += POPCOUNT64(a.Words[i] & b.Words[i]);
		}
		return s;
	}
	static uint32_t PopCountOR(const FaceLayer1024& a, const FaceLayer1024& b)
	{
		uint32_t s = 0;
		for (int i = 0; i < WORDS; ++i)
		{
			s += POPCOUNT64(a.Words[i] | b.Words[i]);
		}
		return s;
	}
	static uint32_t PopCountANDNOT(const FaceLayer1024& a, const FaceLayer1024& b)
	{
		uint32_t s = 0;
		for (int i = 0; i < WORDS; ++i)
		{
			s += POPCOUNT64(a.Words[i] & ~b.Words[i]);
		}
		return s;
	}

	// --- get/set single bit ---
	bool Get(int u, int v) const
	{
		const int bi = linearIndex(u, v);
		const int wi = wordIndex(bi);
		return (Words[wi] >> bitOffset(bi)) & 1ull;
	}
	void Set(int u, int v, bool bState = true)
	{
		const int bi = linearIndex(u, v);
		const int wi = wordIndex(bi);
		const uint64_t m = mask1b(bi);
		if (bState) Words[wi] |= m;
		else       Words[wi] &= ~m;
	}

	// --- pure (returning) bitwise ops ---
	static FaceLayer1024 BitwiseNOT(const FaceLayer1024& a)
	{
		FaceLayer1024 r;
		for (int i = 0; i < WORDS; ++i)
		{
			r.Words[i] = ~a.Words[i];
		}
		return r;
	}
	static FaceLayer1024 BitwiseAND(const FaceLayer1024& a, const FaceLayer1024& b)
	{
		FaceLayer1024 r;
		for (int i = 0; i < WORDS; ++i)
		{
			r.Words[i] = a.Words[i] & b.Words[i];
		}
		return r;
	}
	static FaceLayer1024 BitwiseOR(const FaceLayer1024& a, const FaceLayer1024& b)
	{
		FaceLayer1024 r;
		for (int i = 0; i < WORDS; ++i)
		{
			r.Words[i] = a.Words[i] | b.Words[i];
		}
		return r;
	}
	static FaceLayer1024 BitwiseXOR(const FaceLayer1024& a, const FaceLayer1024& b)
	{
		FaceLayer1024 r;
		for (int i = 0; i < WORDS; ++i)
		{
			r.Words[i] = a.Words[i] ^ b.Words[i];
		}
		return r;
	}
	static FaceLayer1024 BitwiseANDNOT(const FaceLayer1024& a, const FaceLayer1024& b)
	{
		FaceLayer1024 r;
		for (int i = 0; i < WORDS; ++i)
		{
			r.Words[i] = a.Words[i] & ~b.Words[i];
		}
		return r;
	}

private:
	// --- bit addressing helpers ---
	static inline int linearIndex(int u, int v) { return (v << 5) | u; } // row-major: v in [0,31], u in [0,31]
	static inline int wordIndex(int bitIndex) { return bitIndex >> 6; } // /64
	static inline int bitOffset(int bitIndex) { return bitIndex & 63; } // %64
	static inline uint64_t mask1b(int bitIndex) { return 1ull << bitOffset(bitIndex); }
};

// ============================================================================
// extractFaceLayer
// ----------------------------------------------------------------------------
// Extracts a 32×32 bitfield (FaceLayer1024) from a 32×32×32 brick's chosen face.
// Indexing inside a brick uses: li = x | (y<<5) | (z<<10), with x,y,z in [0..31].
struct Brick;
void extractFaceLayer(const Brick& t, FACE_DIR f, FaceLayer1024& out);