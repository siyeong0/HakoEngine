// Bakery main.cpp : Defines the entry point for the application.
#include <iostream>
#include <Windows.h>

#if defined(_MSC_VER) && defined(_DEBUG)
#define CRTDBG_MAP_ALLOC
#include <crtdbg.h>
#endif

#include "Common/Common.h"
#include "Common/StaticMesh.h"
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
		const WCHAR* wchPrelightFileName = nullptr;
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
		WCHAR wchErrTxt[128] = {};
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

	// Load Geometry DLL
	{
		const WCHAR* wchGeometryFileName = nullptr;
#if defined(_M_ARM64EC) || defined(_M_ARM64)
#ifdef _DEBUG
		wchGeometryFileName = L"Geometry_arm64_debug.dll";
#else
		wchGeometryFileName = L"Geometry_arm64_release.dll";
#endif
#elif defined(_M_AMD64)
#ifdef _DEBUG
		wchGeometryFileName = L"Geometry.dll"; // TODO : arm64_debug.dll";
#else
		wchGeometryFileName = L"Geometry.dll";
#endif
#elif defined(_M_IX86)
#ifdef _DEBUG
		wchGeometryFileName = L"Geometry_x86_debug.dll";
#else
		wchGeometryFileName = L"Geometry_x86_release.dll";
#endif
#endif
		WCHAR wchErrTxt[128] = {};
		int	errCode = 0;

		m_hGeometryDLL = LoadLibrary(wchGeometryFileName);
		if (!m_hGeometryDLL)
		{
			errCode = GetLastError();
			swprintf_s(wchErrTxt, L"Fail to LoadLibrary(%s) - Error Code: %u", wchGeometryFileName, errCode);
			ASSERT(false, "Fail to load Geometry DLL");
		}
		CREATE_INSTANCE_FUNC pCreateFunc = (CREATE_INSTANCE_FUNC)GetProcAddress(m_hGeometryDLL, "DllCreateInstance");
		pCreateFunc(&m_pGeometry);
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
		bool bLoaded = mesh.LoadFromFile("../../Resources/Decomp/bunny.off", 10.0f);
		if (bLoaded == false)
		{
			ASSERT(false, "Fail to load mesh file");
			return -1;
		}

		prl::DecomposeToConvex(mesh);
	}

	// Cleanup
	prl::ShutDown();
	m_pPrelight->Cleanup();

	return 0;
}