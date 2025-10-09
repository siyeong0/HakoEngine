#include "QueryPerfCounter.h"
#include <cstdint>
#include <Windows.h>

LARGE_INTEGER	g_Frequency = {};

void QCInit()
{
	QueryPerformanceFrequency(&g_Frequency);
}

LARGE_INTEGER QCGetCounter()
{
	LARGE_INTEGER currCounter;
	QueryPerformanceCounter(&currCounter);
	return currCounter;
}

float QCMeasureElapsedTick(LARGE_INTEGER currCounter, LARGE_INTEGER prevCounter)
{
#ifdef _DEBUG
	if (!g_Frequency.QuadPart)
		__debugbreak();
#endif

	uint64_t elapsedCounter = currCounter.QuadPart - prevCounter.QuadPart;
	float elapsedSec = ((float)elapsedCounter / (float)g_Frequency.QuadPart);
	float elapsedMilSec = elapsedSec * 1000.0f;

	return elapsedMilSec;
}
LARGE_INTEGER QCCounterAddTick(LARGE_INTEGER counter, float tick)
{
#ifdef _DEBUG
	if (!g_Frequency.QuadPart)
		__debugbreak();
#endif
	LARGE_INTEGER result = counter;

	float sec = tick / 1000.0f;
	uint64_t elapsedCounter = (uint64_t)(sec * (float)g_Frequency.QuadPart);
	result.QuadPart += elapsedCounter;

	return result;
}

LARGE_INTEGER QCCounterSubTick(LARGE_INTEGER counter, float tick)
{
#ifdef _DEBUG
	if (!g_Frequency.QuadPart)
		__debugbreak();
#endif
	LARGE_INTEGER result = counter;

	float sec = tick / 1000.0f;
	uint64_t elapsedCounter = (uint64_t)(sec * (float)g_Frequency.QuadPart);
	result.QuadPart -= elapsedCounter;

	return result;
}
