// Bakery main.cpp : Defines the entry point for the application.
#include <iostream>
#include <Windows.h>

#if defined(_MSC_VER) && defined(_DEBUG)
#define CRTDBG_MAP_ALLOC
#include <crtdbg.h>
#endif

#include "Common/Common.h"
#include "Geometry/StaticMesh.h"
#include "Interface/IPrelight.h"
#include "Interface/IGeometry.h"

#include "Atmos.h"

HMODULE m_hPrelightDLL = nullptr;
IPrelight* m_pPrelight = nullptr;

HMODULE m_hGeometryDLL = nullptr;
IGeometry* m_pGeometry = nullptr;

int main()
{
	// Load Prelight DLL
	{
		const wchar_t* wchPrelightFileName = nullptr;
#if defined(_M_ARM64EC) || defined(_M_ARM64)
#ifdef _DEBUG
		wchPrelightFileName = L"Prelight_arm64_debug.dll";
#else
		wchPrelightFileName = L"Prelight_arm64_release.dll";
#endif
#elif defined(_M_AMD64)
#ifdef _DEBUG
		wchPrelightFileName = L"Prelight.dll"; // TODO : arm64_debug.dll";
#else
		wchPrelightFileName = L"Prelight.dll";
#endif
#elif defined(_M_IX86)
#ifdef _DEBUG
		wchPrelightFileName = L"Prelight_x86_debug.dll";
#else
		wchPrelightFileName = L"Prelight_x86_release.dll";
#endif
#endif
		wchar_t wchErrTxt[128] = {};
		int	errCode = 0;

		m_hPrelightDLL = LoadLibrary(wchPrelightFileName);
		if (!m_hPrelightDLL)
		{
			errCode = GetLastError();
			swprintf_s(wchErrTxt, L"Fail to LoadLibrary(%s) - Error Code: %u", wchPrelightFileName, errCode);
			ASSERT(false, "Fail to load Prelight DLL");
		}
		CREATE_INSTANCE_FUNC pCreateFunc = (CREATE_INSTANCE_FUNC)GetProcAddress(m_hPrelightDLL, "DllCreateInstance");
		pCreateFunc(&m_pPrelight);
	}

	// Initialize Bakery
	m_pPrelight->Initialize();
	prl::SetBackend(m_pPrelight);

	// Precompute Atmosphere
	if (false)
	{
		RunAtmosPrecomputeAndSave();
	}

	if (true)
	{
		StaticMesh mesh;
		bool bLoaded = mesh.LoadFromFile("./Resources/stanford_bunny_pbr.glb", 0.05f);
		if (bLoaded == false)
		{
			ASSERT(false, "Fail to load mesh file");
			return -1;
		}

		//mesh = StaticMesh::CreateSphereMesh(1.0f, 16, 8);

		prl::DecomposeToConvex(mesh);
	}

	// Cleanup
	prl::ShutDown();
	m_pPrelight->Cleanup();

	return 0;
}