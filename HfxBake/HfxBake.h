#pragma once
#include "Common/Common.h"
#include "Interface/IHfxBake.h"

class HfxBake : public IHfxBake
{
public:
	// Derived from IHfxBake
	bool ENGINECALL Initialize() override;
	void ENGINECALL Cleanup() override;

	bool ENGINECALL PrecomputeAtmos(const AtmosParams& in, AtmosResult* out) const override;

	// Internal methods
	HfxBake() = default;
	virtual ~HfxBake() { Cleanup(); };

private:

private:

};