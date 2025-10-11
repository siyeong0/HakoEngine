#pragma once
#include <Windows.h>
#include "Common/Common.h"
#include "AtmosStruct.h"

struct StaticMesh;

struct IPrelight
{
	virtual bool ENGINECALL Initialize() = 0;
	virtual void ENGINECALL Cleanup() = 0;

	virtual bool ENGINECALL PrecomputeAtmos(const AtmosParams& in, AtmosResult* out) const = 0;
	virtual bool ENGINECALL DecomposeToConvex(const StaticMesh& m) const = 0;
};

namespace prl
{
	inline const IPrelight* g_pBackend = nullptr;

	inline void SetBackend(IPrelight* impl)
	{
		g_pBackend = impl;
	}

	inline void ShutDown()
	{
		g_pBackend = nullptr;
	}

	inline bool PrecomputeAtmos(const AtmosParams& in, AtmosResult* out)
	{
		ASSERT(g_pBackend, "Prelight backend is not set.");
		return g_pBackend->PrecomputeAtmos(in, out);
	}

	inline bool DecomposeToConvex(const StaticMesh& meshData)
	{
		ASSERT(g_pBackend, "Prelight backend is not set.");
		return g_pBackend->DecomposeToConvex(meshData);
	}
} // namespace hfx