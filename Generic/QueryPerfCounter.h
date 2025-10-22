#pragma once

void QCInit();
LARGE_INTEGER QCGetCounter();
float QCMeasureElapsedTick(LARGE_INTEGER currCounter, LARGE_INTEGER prevCounter);
LARGE_INTEGER QCCounterAddTick(LARGE_INTEGER counter, float tick);
LARGE_INTEGER QCCounterSubTick(LARGE_INTEGER counter, float tick);

