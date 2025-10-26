#pragma once
#include <Windows.h>
#include <combaseapi.h>
#include "Common/Common.h"
#include "Geometry/Geometry.h"

interface IProceduralSphereObject : public IUnknown
{
	virtual bool ENGINECALL BeginCreateGeom(uint numSpheres, const Material& material) = 0;
	virtual bool ENGINECALL InsertSphere(const Sphere& sphereData) = 0;
	virtual void ENGINECALL EndCreateGeom(bool bUseRayTracingIfSupported = true) = 0;

	virtual uint ENGINECALL GetRenderPass() = 0;
};