#include <iostream>
#include <filesystem>
#include <Windows.h>
#include <DirectXMath.h>
#include <shlwapi.h>

#include "Common/Common.h"
#include "Interface/IRenderer.h"
#include "Util/LinkedList.h"
#include "Util/QueryPerfCounter.h"
#include "GameObject.h"
#include "Game.h"


// #define USE_GPU_UPLOAD_HEAPS

bool Game::Initialize(HWND hWnd, bool bEnableDebugLayer, bool bEnableGBV)
{
	const WCHAR* wchRendererFileName = nullptr;

#if defined(_M_ARM64EC) || defined(_M_ARM64)
	#ifdef _DEBUG
		wchRendererFileName = L"RendererD3D12_arm64_debug.dll";
	#else
		wchRendererFileName = L"RendererD3D12_arm64_release.dll";
	#endif
#elif defined(_M_AMD64)
	#ifdef _DEBUG
		wchRendererFileName = L"RendererD3D12.dll"; // TODO : arm64_debug.dll";
	#else
		wchRendererFileName = L"RendererD3D12.dll";
	#endif
#elif defined(_M_IX86)
	#ifdef _DEBUG
		wchRendererFileName = L"RendererD3D12_x86_debug.dll";
	#else
		wchRendererFileName = L"RendererD3D12_x86_release.dll";
	#endif
#endif
	WCHAR wchErrTxt[128] = {};
	int	errCode = 0;

	m_hRendererDLL = LoadLibrary(wchRendererFileName);
	if (!m_hRendererDLL)
	{
		errCode = GetLastError();
		swprintf_s(wchErrTxt, L"Fail to LoadLibrary(%s) - Error Code: %u", wchRendererFileName, errCode);
		MessageBox(hWnd, wchErrTxt, L"Error", MB_OK);
		__debugbreak();
	}
	CREATE_INSTANCE_FUNC	pCreateFunc = (CREATE_INSTANCE_FUNC)GetProcAddress(m_hRendererDLL, "DllCreateInstance");
	pCreateFunc(&m_pRenderer);

	// Get App Path and Set Shader Path
	WCHAR wchShaderPath[_MAX_PATH] = {};
	WCHAR exePath[_MAX_PATH] = {};

	if (GetModuleFileNameW(nullptr, exePath, _MAX_PATH))
	{
		std::filesystem::path exeDir = std::filesystem::path(exePath).parent_path();         // ...\x64\Debug
		std::filesystem::path shaders = (exeDir / L"..\\..\\Shaders").lexically_normal();

		wcsncpy_s(wchShaderPath, shaders.c_str(), _TRUNCATE);      // $(SolutionDir)\Shaders
		// wprintf(L"Shaders = %s\n", wchShaderPath);
	}

	bool bUseGpuUploadHeaps = false;
#ifdef USE_GPU_UPLOAD_HEAPS
	bUseGpuUploadHeaps = true;
#endif

	m_pRenderer->Initialize(hWnd, bEnableDebugLayer, bEnableGBV, bUseGpuUploadHeaps, wchShaderPath);
	m_hWnd = hWnd;
	
	// begin perf check
	LARGE_INTEGER prevCounter = QCGetCounter();

	// Create Font
	m_pFontObj = m_pRenderer->CreateFontObject(L"Tahoma", 18.0f);
	
	// create texture for draw text
	m_TextImageWidth = 512;
	m_TextImageHeight = 64;
	m_pTextImage = (uint8_t*)malloc(m_TextImageWidth * m_TextImageHeight * 4);
	ASSERT(m_pTextImage, "Fail to allocate memory for text image");
	m_pTextTexTexHandle = m_pRenderer->CreateDynamicTexture(m_TextImageWidth, m_TextImageHeight);
	memset(m_pTextImage, 0, m_TextImageWidth * m_TextImageHeight * 4);

	m_pSpriteObjCommon = m_pRenderer->CreateSpriteObject();

	const UINT GAME_OBJ_COUNT = 200;
	for (UINT i = 0; i < GAME_OBJ_COUNT; i++)
	{
		GameObject* pGameObj = createGameObject();
		if (pGameObj)
		{
			float x = (float)((rand() % 51) - 25);	// -10m - 10m 
			float y = (float)((rand() % 3) - 1);	// -1m - 1m
			float z = (float)((rand() % 51) - 25);	// -10m - 10m 
			pGameObj->SetPosition(x, y, z);
			float rad = (rand() % 181) * (3.1415f / 180.0f);
			pGameObj->SetRotationY(rad);
			float scale = 0.5f * (float)((rand() % 10) + 1);	// 1 - 3
			pGameObj->SetScale(scale, scale, scale);
		}
	}
	
	// end perf check
	float fElpasedTick = QCMeasureElapsedTick(QCGetCounter(), prevCounter);
	
	WCHAR wchTxt[128] = {};
	swprintf_s(wchTxt, L"App Initialized. GPU-UploadHeaps:%s, %.2f ms elapsed.\n", m_pRenderer->IsGpuUploadHeapsEnabled() ? L"Enabled" : L"N/A", fElpasedTick);
	OutputDebugStringW(wchTxt);

	m_pRenderer->SetCameraPos(0.0f, 0.0f, -10.0f);
	return true;
}

void Game::Run()
{
	m_FrameCount++;

	// begin
	uint64_t currTick = GetTickCount64();

	// game business logic
	Update(currTick);

	render();

	if (currTick - m_PrevFrameCheckTick > 1000)
	{
		m_PrevFrameCheckTick = currTick;	
				
		WCHAR wchTxt[64];
		m_FPS = m_FrameCount;
		m_NumCommandLists = m_pRenderer->GetCommandListCount();

		swprintf_s(wchTxt, L"FPS : %u, CommandList : %u ", m_FPS, m_NumCommandLists);
		SetWindowText(m_hWnd, wchTxt);
				
		m_FrameCount = 0;
	}
}

bool Game::Update(uint64_t currTick)
{	
	// Update Scene with 60FPS
	if (currTick - m_PrevUpdateTick < 16)
	{
		return false;
	}
	m_PrevUpdateTick = currTick;

	// Update camra
	if (m_CamOffsetX != 0.0f || m_CamOffsetY != 0.0f || m_CamOffsetZ != 0.0f)
	{
		m_pRenderer->MoveCamera(m_CamOffsetX, m_CamOffsetY, m_CamOffsetZ);
	}
	
	// update game objects
	SORT_LINK* pCur = m_pGameObjLinkHead;
	while (pCur)
	{
		GameObject* pGameObj = (GameObject*)pCur->pItem;
		pGameObj->Run();
		pCur = pCur->pNext;
	}
	
	// update status text
	int iTextWidth = 0;
	int iTextHeight = 0;
	WCHAR wchTxt[64] = {};
	UINT txtLen = swprintf_s(wchTxt, L"FrameRate: %u, CommandList: %u", m_FPS, m_NumCommandLists);

	if (wcscmp(m_wchText, wchTxt))
	{
		// 텍스트가 변경된 경우
		memset(m_pTextImage, 0, m_TextImageWidth * m_TextImageHeight * 4);
		m_pRenderer->WriteTextToBitmap(m_pTextImage, m_TextImageWidth, m_TextImageHeight, m_TextImageWidth * 4, &iTextWidth, &iTextHeight, m_pFontObj, wchTxt, txtLen);
		m_pRenderer->UpdateTextureWithImage(m_pTextTexTexHandle, m_pTextImage, m_TextImageWidth, m_TextImageHeight);
		wcscpy_s(m_wchText, wchTxt);
	}

	return true;
}

void Game::Cleanup()
{
	deleteAllGameObjects();

	if (m_pTextImage)
	{
		free(m_pTextImage);
		m_pTextImage = nullptr;
	}
	if (m_pRenderer)
	{
		if (m_pFontObj)
		{
			m_pRenderer->DeleteFontObject(m_pFontObj);
			m_pFontObj = nullptr;
		}

		if (m_pTextTexTexHandle)
		{
			m_pRenderer->DeleteTexture(m_pTextTexTexHandle);
			m_pTextTexTexHandle = nullptr;
		}
		if (m_pSpriteObjCommon)
		{
			m_pSpriteObjCommon->Release();
			m_pSpriteObjCommon = nullptr;
		}

		m_pRenderer->Release();
		m_pRenderer = nullptr;
	}
	if (m_hRendererDLL)
	{
		FreeLibrary(m_hRendererDLL);
		m_hRendererDLL = nullptr;
	}
}

void Game::OnKeyDown(UINT nChar, UINT uiScanCode)
{
	switch (nChar)
	{
	case VK_SHIFT:
		m_bShiftKeyDown = true;
		break;
	case 'W':
		if (m_bShiftKeyDown)
		{
			m_CamOffsetY = 0.05f;
		}
		else
		{
			m_CamOffsetZ = 0.05f;
		}
		break;
	case 'S':
		if (m_bShiftKeyDown)
		{
			m_CamOffsetY = -0.05f;
		}
		else
		{
			m_CamOffsetZ = -0.05f;
		}
		break;
	case 'A':
		m_CamOffsetX = -0.05f;
		break;
	case 'D':
		m_CamOffsetX = 0.05f;
		break;
	}
}

void Game::OnKeyUp(UINT nChar, UINT uiScanCode)
{
	switch (nChar)
	{
	case VK_SHIFT:
		m_bShiftKeyDown = false;
		break;
	case 'W':
		m_CamOffsetY = 0.0f;
		m_CamOffsetZ = 0.0f;
		break;
	case 'S':
		m_CamOffsetY = 0.0f;
		m_CamOffsetZ = 0.0f;
		break;
	case 'A':
		m_CamOffsetX = 0.0f;
		break;
	case 'D':
		m_CamOffsetX = 0.0f;
		break;
	}
}

void Game::OnMouseLButtonDown(int x, int y, UINT nFlags)
{
	m_bMouseLButtonDown = true;
}

void Game::OnMouseLButtonUp(int x, int y, UINT nFlags)
{
	m_bMouseLButtonDown = false;
}

void Game::OnMouseRButtonDown(int x, int y, UINT nFlags)
{
	m_bCamRotMode = true;
	m_MouseXRButtonPressed = x;
	m_MouseYRButtonPressed = y;

	m_bMouseRButtonDown = true;
}

void Game::OnMouseRButtonUp(int x, int y, UINT nFlags)
{
	m_bCamRotMode = false;
	m_bMouseRButtonDown = false;
}

void Game::OnMouseMButtonDown(int x, int y, UINT nFlags)
{
	m_bMouseMButtonDown = true;
}

void Game::OnMouseMButtonUp(int x, int y, UINT nFlags)
{
	m_bMouseMButtonDown = false;
}

void Game::OnMouseMove(int x, int y, UINT nFlags)
{
	m_PrevMouseX = m_CurrMouseX;
	m_PrevMouseY = m_CurrMouseY;

	int dx = x - m_PrevMouseX;
	int dy = y - m_PrevMouseY;

	if (m_bCamRotMode)
	{
		if (dy != 0)
			int a = 0;

		float fYaw = (float)dx * 0.01f;
		float fPitch = (float)dy * 0.01f;
		m_pRenderer->SetCameraRot(fYaw, fPitch, 0.0f);
	}
	m_CurrMouseX = x;
	m_CurrMouseY = y;
}

void Game::OnMouseWheel(int x, int y, int iWheel)
{
}

void Game::OnMouseHWheel(int x, int y, int iWheel)
{
}

bool Game::UpdateWindowSize(uint32_t backBufferWidth, uint32_t backBufferHeight)
{
	bool bResult = false;
	if (m_pRenderer)
	{
		bResult = m_pRenderer->UpdateWindowSize(backBufferWidth, backBufferHeight);
	}
	return bResult;
}

void Game::render()
{
	m_pRenderer->BeginRender();

	// render game objects
	SORT_LINK* pCur = m_pGameObjLinkHead;
	DWORD dwObjCount = 0;
	while (pCur)
	{
		GameObject* pGameObj = (GameObject*)pCur->pItem;
		pGameObj->Render();
		pCur = pCur->pNext;
		dwObjCount++;
	}
	// render dynamic texture as text
	m_pRenderer->RenderSpriteWithTex(m_pSpriteObjCommon,
		5, 5,
		1.0f, 1.0f,
		nullptr, 0.0f, m_pTextTexTexHandle);

	m_pRenderer->EndRender();
	m_pRenderer->Present();
}

GameObject* Game::createGameObject()
{
	GameObject* pGameObj = new GameObject;
	pGameObj->Initialize(this);
	LinkToLinkedListFIFO(&m_pGameObjLinkHead, &m_pGameObjLinkTail, &pGameObj->m_LinkInGame);

	return pGameObj;
}

void Game::deleteGameObject(GameObject* pGameObj)
{
	UnLinkFromLinkedList(&m_pGameObjLinkHead, &m_pGameObjLinkTail, &pGameObj->m_LinkInGame);
	delete pGameObj;
}

void Game::deleteAllGameObjects()
{
	while (m_pGameObjLinkHead)
	{
		GameObject* pGameObj = (GameObject*)m_pGameObjLinkHead->pItem;
		deleteGameObject(pGameObj);
	}
}