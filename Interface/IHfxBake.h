#pragma once
#include <Windows.h>
#include "Common/Common.h"
#include "AtmosStruct.h"

struct IHfxBake
{
	virtual bool ENGINECALL Initialize() = 0;
	virtual void ENGINECALL Cleanup() = 0;

	virtual bool ENGINECALL PrecomputeAtmos(const AtmosParams& in, AtmosResult* out) const = 0;
};

namespace hfx
{
	inline const IHfxBake* g_pBackend = nullptr;

	inline void SetBackend(IHfxBake* impl)
	{
		g_pBackend = impl;
	}

	inline void ShutDown()
	{
		g_pBackend = nullptr;
	}

	inline bool PrecomputeAtmos(const AtmosParams& in, AtmosResult* out)
	{
		ASSERT(g_pBackend, "IHfxBake backend is not set.");
		return g_pBackend->PrecomputeAtmos(in, out);
	}
} // namespace hfx