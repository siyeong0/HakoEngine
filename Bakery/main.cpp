// Bakery main.cpp : Defines the entry point for the application.
#include <iostream>
#include <Windows.h>

#if defined(_MSC_VER) && defined(_DEBUG)
#define CRTDBG_MAP_ALLOC
#include <crtdbg.h>
#endif

#include "Common/Common.h"
#include "Interface/IHfxBake.h"

#include "Atmos.h"

HMODULE m_hHfxBakeDLL = nullptr;
IHfxBake* m_pHfxBake = nullptr;

int main()
{
	// Load Bakery DLL
	{
		const WCHAR* wchHfxBakeFileName = nullptr;
#if defined(_M_ARM64EC) || defined(_M_ARM64)
#ifdef _DEBUG
		wchHfxBakeFileName = L"HfxBake_arm64_debug.dll";
#else
		wchHfxBakeFileName = L"HfxBake_arm64_release.dll";
#endif
#elif defined(_M_AMD64)
#ifdef _DEBUG
		wchHfxBakeFileName = L"HfxBake.dll"; // TODO : arm64_debug.dll";
#else
		wchHfxBakeFileName = L"HfxBake.dll";
#endif
#elif defined(_M_IX86)
#ifdef _DEBUG
		wchHfxBakeFileName = L"HfxBake_x86_debug.dll";
#else
		wchHfxBakeFileName = L"HfxBake_x86_release.dll";
#endif
#endif
		WCHAR wchErrTxt[128] = {};
		int	errCode = 0;

		m_hHfxBakeDLL = LoadLibrary(wchHfxBakeFileName);
		if (!m_hHfxBakeDLL)
		{
			errCode = GetLastError();
			swprintf_s(wchErrTxt, L"Fail to LoadLibrary(%s) - Error Code: %u", wchHfxBakeFileName, errCode);

			ASSERT(false, "Fail to load HfxBake DLL");
		}
		CREATE_INSTANCE_FUNC pCreateFunc = (CREATE_INSTANCE_FUNC)GetProcAddress(m_hHfxBakeDLL, "DllCreateInstance");
		pCreateFunc(&m_pHfxBake);
	}

	// Initialize Bakery
	m_pHfxBake->Initialize();
	hfx::SetBackend(m_pHfxBake);

	// Precompute Atmosphere
	{
		RunAtmosPrecomputeAndSave();
	}

	// Cleanup
	hfx::ShutDown();
	m_pHfxBake->Cleanup();

	return 0;
}