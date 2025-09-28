#pragma once
#include <Windows.h>
#include <combaseapi.h>
#include "Common/Common.h"

interface IMeshObject : public IUnknown
{
	virtual bool ENGINECALL BeginCreateMesh(const Vertex* vertices, size_t numVertices, size_t numTriGroups) = 0;
	virtual bool ENGINECALL InsertTriGroup(const uint16_t* indices, size_t numTriangles, const WCHAR* wchTexFileName) = 0;
	virtual void ENGINECALL EndCreateMesh() = 0;
};