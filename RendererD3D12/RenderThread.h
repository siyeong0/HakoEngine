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
	uint ThreadIndex;
	HANDLE hThread;
	HANDLE hEventList[RENDER_THREAD_EVENT_TYPE_COUNT];
};

uint WINAPI RenderThread(void* pArg);