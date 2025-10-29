// dllmain.cpp : Defines the entry point for the DLL application.
#include "pch.h"

BOOL APIENTRY DllMain( HMODULE hModule,
                       DWORD  ul_reason_for_call,
                       LPVOID lpReserved
                     )
{
    switch (ul_reason_for_call)
    {
    case DLL_PROCESS_ATTACH:
    case DLL_THREAD_ATTACH:
    case DLL_THREAD_DETACH:
    case DLL_PROCESS_DETACH:
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
