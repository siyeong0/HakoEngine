#include "pch.h"

#include <process.h>

#include "D3D12Renderer.h"

#include "RenderThread.h"

class D3D12Renderer;

UINT WINAPI RenderThread(void* pArg)
{
	RenderThreadDesc* pDesc = (RenderThreadDesc*)pArg;
	D3D12Renderer* pRenderer = pDesc->pRenderer;
	UINT threadIndex = pDesc->ThreadIndex;
	const HANDLE* phEventList = pDesc->hEventList;
	while (1)
	{
		UINT eventIndex = WaitForMultipleObjects(RENDER_THREAD_EVENT_TYPE_COUNT, phEventList, FALSE, INFINITE);

		switch (eventIndex)
		{
			case RENDER_THREAD_EVENT_TYPE_PROCESS:
				pRenderer->ProcessByThread(threadIndex);
				break;
			case RENDER_THREAD_EVENT_TYPE_DESTROY:
				goto lb_exit;
			default:
				__debugbreak();
		}
	}
lb_exit:
	_endthreadex(0);
	return 0;
}