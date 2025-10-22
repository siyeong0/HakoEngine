#pragma once
#include <Windows.h>
#include <combaseapi.h>
#include "Common/Common.h"
#include "Geometry/Geometry.h"

interface IMeshObject : public IUnknown
{
	virtual void ENGINECALL CreateFromStaticMesh(const StaticMesh & mesh, bool bUseRayTracingIfSupported = true) = 0;

	virtual bool ENGINECALL BeginCreateMesh(const Vertex * vertices, uint numVertices, uint numTriGroups) = 0;
	virtual bool ENGINECALL InsertTriGroup(const uint16_t* indices, uint numTriangles, const Material& material) = 0;
	virtual void ENGINECALL EndCreateMesh(bool bUseRayTracingIfSupported = true) = 0;

	virtual uint ENGINECALL GetRenderPass() = 0;
};