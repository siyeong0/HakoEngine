#pragma once
#include <Windows.h>
#include <combaseapi.h>
#include "Common/Common.h"

interface IGeometry : public IUnknown
{
	virtual bool ENGINECALL Initialize() = 0;
	virtual void ENGINECALL Cleanup() = 0;
};