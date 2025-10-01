﻿// dllmain.cpp : Defines the entry point for the DLL application.
#include "pch.h"
#include "D3D12Renderer.h"

// required .lib files
#pragma comment(lib, "DXGI.lib")
#pragma comment(lib, "dxguid.lib")
#pragma comment(lib, "D3D12.lib")
#pragma comment( lib, "d3d11.lib" )
#pragma comment(lib, "D3DCompiler.lib")
#pragma comment(lib, "d2d1.lib")
#pragma comment(lib, "dwrite.lib")
#pragma comment(lib, "DirectXTex.lib")

bool ENGINECALL DllMain(HMODULE hModule,
	DWORD  ul_reason_for_call,
	LPVOID lpReserved
)
{
	switch (ul_reason_for_call)
	{
	case DLL_PROCESS_ATTACH:
	{
#ifdef _DEBUG
		int	flags = _CRTDBG_ALLOC_MEM_DF | _CRTDBG_LEAK_CHECK_DF;
		_CrtSetDbgFlag(flags);
#endif
	}
	break;
	case DLL_THREAD_ATTACH:
	case DLL_THREAD_DETACH:
		break;
	case DLL_PROCESS_DETACH:
	{
#ifdef _DEBUG
		_ASSERT(_CrtCheckMemory());
#endif
	}
	break;
	}
	return TRUE;
}


extern "C" __declspec(dllexport)
HRESULT WINAPI DllCreateInstance(void** ppv)
{
	HRESULT hr;
	IRenderer* pRenderer = new D3D12Renderer;

	if (!pRenderer)
	{
		hr = E_OUTOFMEMORY;
		goto lb_return;
	}
	hr = S_OK;
	*ppv = pRenderer;

lb_return:
	return hr;
}