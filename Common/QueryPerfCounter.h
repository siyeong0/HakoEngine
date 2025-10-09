#pragma once
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>

void QCInit();
LARGE_INTEGER QCGetCounter();
float QCMeasureElapsedTick(LARGE_INTEGER currCounter, LARGE_INTEGER prevCounter);
LARGE_INTEGER QCCounterAddTick(LARGE_INTEGER counter, float tick);
LARGE_INTEGER QCCounterSubTick(LARGE_INTEGER counter, float tick);

