// App.cpp : Defines the entry point for the application.
#include <Windows.h>
#include <windowsx.h>
#include "Resource.h"
#include "Game.h"

#if defined(_MSC_VER) && defined(_DEBUG)
#define CRTDBG_MAP_ALLOC
#include <crtdbg.h>
#endif


extern "C" { __declspec(dllexport) DWORD NvOptimusEnablement = 0x00000001; }

//////////////////////////////////////////////////////////////////////////////////////////////////////
// D3D12 Agility SDK Runtime

extern "C" { __declspec(dllexport) extern const UINT D3D12SDKVersion = 616; }

#if defined(_M_ARM64EC)
extern "C" { __declspec(dllexport) extern const char* D3D12SDKPath = ".\\D3D12\\arm64\\"; }
#elif defined(_M_ARM64)
extern "C" { __declspec(dllexport) extern const char* D3D12SDKPath = ".\\D3D12\\arm64\\"; }
#elif defined(_M_AMD64)
extern "C" { __declspec(dllexport) extern const char* D3D12SDKPath = ".\\D3D12\\"; }
#elif defined(_M_IX86)
extern "C" { __declspec(dllexport) extern const char* D3D12SDKPath = ".\\D3D12\\"; }
#endif
//////////////////////////////////////////////////////////////////////////////////////////////////////

#pragma comment(lib, "Shlwapi.lib")


static constexpr UINT MAX_LOADSTRING = 100;

// Global Variables:
Game* g_pGame = nullptr;
HINSTANCE hInst;                                // current instance
HWND g_hMainWindow = nullptr;
WCHAR szTitle[MAX_LOADSTRING];                  // The title bar text
WCHAR szWindowClass[MAX_LOADSTRING];            // the main window class name

// Forward declarations of functions included in this code module:
ATOM                MyRegisterClass(HINSTANCE hInstance);
BOOL                InitInstance(HINSTANCE, int);
LRESULT CALLBACK    WndProc(HWND, UINT, WPARAM, LPARAM);
INT_PTR CALLBACK    About(HWND, UINT, WPARAM, LPARAM);

int WINAPI wWinMain(_In_ HINSTANCE hInstance,
	_In_opt_ HINSTANCE hPrevInstance,
	_In_ LPWSTR    lpCmdLine,
	_In_ int       nCmdShow)
{
	UNREFERENCED_PARAMETER(hPrevInstance);
	UNREFERENCED_PARAMETER(lpCmdLine);

#ifdef _DEBUG
	_CrtSetDbgFlag(_CRTDBG_ALLOC_MEM_DF | _CRTDBG_LEAK_CHECK_DF);
#endif
	// Initialize global strings
	LoadStringW(hInstance, IDS_APP_TITLE, szTitle, MAX_LOADSTRING);
	LoadStringW(hInstance, IDC_HELLOFLECS, szWindowClass, MAX_LOADSTRING);
	MyRegisterClass(hInstance);

	// Perform application initialization:
	if (!InitInstance(hInstance, nCmdShow))
	{
		return FALSE;
	}

	HACCEL hAccelTable = LoadAccelerators(hInstance, MAKEINTRESOURCE(IDC_HELLOFLECS));

	MSG msg;

	g_pGame = new Game;
	//g_pGame->Initialize(g_hMainWindow, TRUE, TRUE);
	g_pGame->Initialize(g_hMainWindow, false, false);

	SetWindowText(g_hMainWindow, L"Dll Renderer");
	// Main message loop:
	while (1)
	{
		// call WndProc
		//g_bCanUseWndProc == FALSE이면 DefWndProc호출

		if (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE))
		{
			if (msg.message == WM_QUIT)
			{
				break;
			}
			TranslateMessage(&msg);
			DispatchMessage(&msg);


		}
		else
		{
			if (g_pGame)
			{
				g_pGame->Run();
			}
		}
	}
	if (g_pGame)
	{
		delete g_pGame;
		g_pGame = nullptr;
	}
#ifdef _DEBUG
	_ASSERT(_CrtCheckMemory());
#endif
	return (int)msg.wParam;
}



//
//  FUNCTION: MyRegisterClass()
//
//  PURPOSE: Registers the window class.
//
ATOM MyRegisterClass(HINSTANCE hInstance)
{
	WNDCLASSEXW wcex = {};

	wcex.cbSize = sizeof(WNDCLASSEX);

	wcex.style = CS_HREDRAW | CS_VREDRAW;
	wcex.lpfnWndProc = WndProc;
	wcex.cbClsExtra = 0;
	wcex.cbWndExtra = 0;
	wcex.hInstance = hInstance;
	wcex.hIcon = LoadIcon(hInstance, MAKEINTRESOURCE(IDC_HELLOFLECS));
	wcex.hCursor = LoadCursor(nullptr, IDC_ARROW);
	wcex.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
	wcex.lpszMenuName = MAKEINTRESOURCEW(IDC_HELLOFLECS);
	wcex.lpszClassName = szWindowClass;
	wcex.hIconSm = LoadIcon(wcex.hInstance, MAKEINTRESOURCE(IDI_SMALL));

	return RegisterClassExW(&wcex);
}

//
//   FUNCTION: InitInstance(HINSTANCE, int)
//
//   PURPOSE: Saves instance handle and creates main window
//
//   COMMENTS:
//
//        In this function, we save the instance handle in a global variable and
//        create and display the main program window.
//
BOOL InitInstance(HINSTANCE hInstance, int nCmdShow)
{
	hInst = hInstance; // Store instance handle in our global variable

	HWND hWnd = CreateWindowW(szWindowClass, szTitle, WS_OVERLAPPEDWINDOW,
		CW_USEDEFAULT, 0, CW_USEDEFAULT, 0, nullptr, nullptr, hInstance, nullptr);

	if (!hWnd)
	{
		return FALSE;
	}
	g_hMainWindow = hWnd;
	ShowWindow(hWnd, nCmdShow);
	UpdateWindow(hWnd);

	return TRUE;
}

//
//  FUNCTION: WndProc(HWND, UINT, WPARAM, LPARAM)
//
//  PURPOSE: Processes messages for the main window.
//
//  WM_COMMAND  - process the application menu
//  WM_PAINT    - Paint the main window
//  WM_DESTROY  - post a quit message and return
//
LRESULT CALLBACK WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
	int iMouseX = 0;
	int iMouseY = 0;
	switch (message)
	{
	case WM_COMMAND:
	{
		int wmId = LOWORD(wParam);
		// Parse the menu selections:
		switch (wmId)
		{
		case IDM_ABOUT:
			DialogBox(hInst, MAKEINTRESOURCE(IDD_ABOUTBOX), hWnd, About);
			break;
		case IDM_EXIT:
			DestroyWindow(hWnd);
			break;
		default:
			return DefWindowProc(hWnd, message, wParam, lParam);
		}
	}
	break;
	case WM_PAINT:
	{
		PAINTSTRUCT ps;
		HDC hdc = BeginPaint(hWnd, &ps);
		// TODO: Add any drawing code that uses hdc here...
		EndPaint(hWnd, &ps);
	}
	break;
	case WM_SIZE:
	{
		if (g_pGame)
		{
			RECT	rect;
			GetClientRect(hWnd, &rect);
			DWORD	dwWndWidth = rect.right - rect.left;
			DWORD	dwWndHeight = rect.bottom - rect.top;
			g_pGame->UpdateWindowSize(dwWndWidth, dwWndHeight);
		}
	}
	break;
	case WM_MOUSEMOVE:
		if (g_pGame)
		{
			iMouseX = GET_X_LPARAM(lParam);
			iMouseY = GET_Y_LPARAM(lParam);
			g_pGame->OnMouseMove(iMouseX, iMouseY, (UINT)wParam);
		}
		break;

	case WM_LBUTTONDOWN:
		if (g_pGame)
		{
			iMouseX = GET_X_LPARAM(lParam);
			iMouseY = GET_Y_LPARAM(lParam);
			g_pGame->OnMouseLButtonDown(iMouseX, iMouseY, (UINT)wParam);
		}
		break;
	case WM_LBUTTONUP:
		if (g_pGame)
		{
			iMouseX = GET_X_LPARAM(lParam);
			iMouseY = GET_Y_LPARAM(lParam);
			g_pGame->OnMouseLButtonUp(iMouseX, iMouseY, (UINT)wParam);
		}
		break;
	case WM_RBUTTONDOWN:
		if (g_pGame)
		{
			iMouseX = GET_X_LPARAM(lParam);
			iMouseY = GET_Y_LPARAM(lParam);
			g_pGame->OnMouseRButtonDown(iMouseX, iMouseY, (UINT)wParam);
		}
		break;
	case WM_RBUTTONUP:
		if (g_pGame)
		{
			iMouseX = GET_X_LPARAM(lParam);
			iMouseY = GET_Y_LPARAM(lParam);
			g_pGame->OnMouseRButtonUp(iMouseX, iMouseY, (UINT)wParam);
		}
		break;
	case WM_MBUTTONDOWN:
		if (g_pGame)
		{
			iMouseX = GET_X_LPARAM(lParam);
			iMouseY = GET_Y_LPARAM(lParam);
			g_pGame->OnMouseMButtonDown(iMouseX, iMouseY, (UINT)wParam);
		}
		break;
	case WM_MBUTTONUP:
		if (g_pGame)
		{
			iMouseX = GET_X_LPARAM(lParam);
			iMouseY = GET_Y_LPARAM(lParam);
			g_pGame->OnMouseMButtonUp(iMouseX, iMouseY, (UINT)wParam);
		}
		break;
	case WM_KEYDOWN:
	{
		if (g_pGame)
		{
			UINT	uiScanCode = (0x00ff0000 & lParam) >> 16;
			UINT	vkCode = MapVirtualKey(uiScanCode, MAPVK_VSC_TO_VK);
			if (!(lParam & 0x40000000))
			{
				g_pGame->OnKeyDown(vkCode, uiScanCode);

			}
		}
	}
	break;

	case WM_KEYUP:
	{
		if (g_pGame)
		{
			UINT	uiScanCode = (0x00ff0000 & lParam) >> 16;
			UINT	vkCode = MapVirtualKey(uiScanCode, MAPVK_VSC_TO_VK);
			g_pGame->OnKeyUp(vkCode, uiScanCode);
		}
	}
	break;
	case WM_DESTROY:
		PostQuitMessage(0);
		break;
	default:
		return DefWindowProc(hWnd, message, wParam, lParam);
	}
	return 0;
}

// Message handler for about box.
INT_PTR CALLBACK About(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam)
{
	UNREFERENCED_PARAMETER(lParam);
	switch (message)
	{
	case WM_INITDIALOG:
		return (INT_PTR)TRUE;

	case WM_COMMAND:
		if (LOWORD(wParam) == IDOK || LOWORD(wParam) == IDCANCEL)
		{
			EndDialog(hDlg, LOWORD(wParam));
			return (INT_PTR)TRUE;
		}
		break;
	}
	return (INT_PTR)FALSE;
}
