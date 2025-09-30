#include "pch.h"
#include "HfxBake.h"

#include <iostream>

bool ENGINECALL HfxBake::Initialize()
{
	std::cout << "HfxBake::Initialize called." << std::endl;
	return true;
}

void ENGINECALL HfxBake::Cleanup()
{
	std::cout << "HfxBake::Cleanup called." << std::endl;
}

bool ENGINECALL HfxBake::PrecomputeAtmos(const AtmosParams& in, AtmosResult* out) const
{
	std::cout << "HfxBake::PrecomputeAtmos called." << std::endl;

	return true;
}