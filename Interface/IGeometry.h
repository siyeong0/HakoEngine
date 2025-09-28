#pragma once
#include <Windows.h>
#include <combaseapi.h>
#include "Common/Common.h"
#include "Common/MeshData.h"

interface IGeometry : public IUnknown
{
	virtual bool ENGINECALL Initialize() = 0;
	virtual void ENGINECALL Cleanup() = 0;

	virtual MeshData ENGINECALL CreateUnitCubeMesh() = 0;
	virtual MeshData ENGINECALL CreateBoxMesh(float width, float height, float depth) = 0;
	virtual MeshData ENGINECALL CreateSphereMesh(float radius, int segments, int rings) = 0;
	virtual MeshData ENGINECALL CreateGridMesh(float width, float height, int rows, int columns) = 0;
	virtual MeshData ENGINECALL CreateCylinderMesh(float radius, float height, int segments) = 0;
	virtual MeshData ENGINECALL CreateConeMesh(float radius, float height, int segments) = 0;
	virtual MeshData ENGINECALL CreatePlaneMesh(float width, float height) = 0;
};