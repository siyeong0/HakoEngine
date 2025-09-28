#pragma once
#include "Common/Common.h"
#include "Interface/IGeometry.h"

class Geometry : public IGeometry
{
public:
	// Derived from IUnknown
	STDMETHODIMP			QueryInterface(REFIID, void** ppv) override;
	STDMETHODIMP_(ULONG)	AddRef() override;
	STDMETHODIMP_(ULONG)	Release() override;

	// Derived from IGeometry
	bool ENGINECALL Initialize() override;
	void ENGINECALL Cleanup() override;

	MeshData ENGINECALL CreateUnitCubeMesh() override;
	MeshData ENGINECALL CreateBoxMesh(float width, float height, float depth) override;
	MeshData ENGINECALL CreateSphereMesh(float radius, int segments, int rings) override;
	MeshData ENGINECALL CreateGridMesh(float width, float height, int rows, int columns) override;
	MeshData ENGINECALL CreateCylinderMesh(float radius, float height, int segments) override;
	MeshData ENGINECALL CreateConeMesh(float radius, float height, int segments) override;
	MeshData ENGINECALL CreatePlaneMesh(float width, float height) override;

	// Interal methods
	Geometry() = default;
	virtual ~Geometry() { Cleanup(); };

private:
	int m_RefCount = 1;
};