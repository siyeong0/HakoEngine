#include "pch.h"
#include "Common/Vertex.h"
#include "Geometry.h"

// --------------------------------------------------
// Unknown methods
// --------------------------------------------------

STDMETHODIMP Geometry::QueryInterface(REFIID refiid, void** ppv)
{
	return E_NOINTERFACE;
}

STDMETHODIMP_(ULONG) Geometry::AddRef()
{
	m_RefCount++;
	return m_RefCount;
}

STDMETHODIMP_(ULONG) Geometry::Release()
{
	DWORD ref_count = --m_RefCount;
	if (!m_RefCount)
		delete this;
	return ref_count;
}

// --------------------------------------------------
// IGeometry methods
// --------------------------------------------------

bool ENGINECALL Geometry::Initialize()
{
	return true;
}

void ENGINECALL Geometry::Cleanup()
{
}
