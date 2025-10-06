#pragma once
#include "Common/Common.h"
#include "Interface/IPrelight.h"

class Prelight : public IPrelight
{
public:
	// Derived from IPrelight
	bool ENGINECALL Initialize() override;
	void ENGINECALL Cleanup() override;

	bool ENGINECALL PrecomputeAtmos(const AtmosParams& in, AtmosResult* out) const override;

	// Internal methods
	Prelight() = default;
	virtual ~Prelight() { Cleanup(); };

private:

private:

};