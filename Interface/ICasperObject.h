#pragma once
#include <Windows.h>
#include <string>
#include <combaseapi.h>
#include "Common/Common.h"

struct CasperAtlas
{
	Bounds AtlasBounds;
	std::wstring DiffuseAtlas;
	std::wstring DepthAtlas;
};

interface ICasperObject : public IUnknown
{
	virtual bool ENGINECALL BeginCreateCasper(uint numAtlases, const UMaterial & material) = 0;
	virtual bool ENGINECALL InsertCasperAtlas(const CasperAtlas& atlas) = 0;
	virtual void ENGINECALL EndCreateCasper(bool bUseRayTracingIfSupported = true) = 0;

	virtual uint ENGINECALL GetRenderPass() = 0;
};