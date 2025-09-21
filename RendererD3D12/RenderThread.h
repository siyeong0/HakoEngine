#pragma once

enum ERenderThreadEventType
{
	RENDER_THREAD_EVENT_TYPE_PROCESS,
	RENDER_THREAD_EVENT_TYPE_DESTROY,
	RENDER_THREAD_EVENT_TYPE_COUNT
};

struct RenderThreadDesc
{
	D3D12Renderer* pRenderer;
	UINT ThreadIndex;
	HANDLE hThread;
	HANDLE hEventList[RENDER_THREAD_EVENT_TYPE_COUNT];
};

UINT WINAPI RenderThread(void* pArg);