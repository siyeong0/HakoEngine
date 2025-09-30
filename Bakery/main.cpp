// Bakery main.cpp : Defines the entry point for the application.
#include <iostream>
#include <Windows.h>

#if defined(_MSC_VER) && defined(_DEBUG)
#define CRTDBG_MAP_ALLOC
#include <crtdbg.h>
#endif

#include "Common/Common.h"
#include "Interface/IHfxBake.h"


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
		AtmosParams atmosParams = {};
		atmosParams.PlanetRadius = 6360000.0f;        // Radius of the planet in meters
		atmosParams.AtmosphereHeight = 100000.0f;    // Height of the atmosphere in meters
		atmosParams.RayleighScattering = 5.8e-6f;  // Rayleigh scattering coefficient
		atmosParams.MieScattering = 3.0e-6f;       // Mie scattering coefficient
		atmosParams.SunIntensity = 20.0f;        // Intensity of the sun
		atmosParams.SunAngularDiameter = 0.53f;  // Angular diameter of the sun in degrees
		atmosParams.MieG = 0.76f;               // Mie phase function asymmetry factor

		AtmosResult atmosResult = {};
		hfx::PrecomputeAtmos(atmosParams, &atmosResult);
	}

	// Cleanup
	hfx::ShutDown();
	m_pHfxBake->Cleanup();

	return 0;
}