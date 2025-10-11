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

	// Interal methods
	Geometry() = default;
	~Geometry() { Cleanup(); };

private:
	int m_RefCount = 1;
};