#include "pch.h"
#include "Brick.h"
#include "FaceLayer1024.h"

void extractFaceLayer(const Brick& t, FACE_DIR f, FaceLayer1024& out)
{
	out.Clear();
	if (t.Mode == Brick::FULL)
	{
		out.SetAll();
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