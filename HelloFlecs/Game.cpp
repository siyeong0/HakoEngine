#include <iostream>
#include <format>
#include <filesystem>
#include <Windows.h>
#include <DirectXMath.h>
#include <shlwapi.h>

#include "Common/Common.h"
#include "Interface/IRenderer.h"
#include "Util/LinkedList.h"
#include "Util/QueryPerfCounter.h"
#include "Util/VertexUtil.h"
#include "Game.h"
#include "Component.h"


// #define USE_GPU_UPLOAD_HEAPS

// TODO: Separate to another file or project
static IMeshObject* createBoxMeshObject(IRenderer* pRenderer)
{
	IMeshObject* pMeshObj = nullptr;

	// create box mesh
	// create vertices and indices
	uint16_t indices[36] = {};
	BasicVertex* vertices = nullptr;
	uint32_t numVertices = CreateBoxMesh(&vertices, indices, (uint32_t)_countof(indices), 0.25f);

	// create BasicMeshObject from Renderer
	pMeshObj = pRenderer->CreateBasicMeshObject();

	const WCHAR* wchTexFileNameList[6] =
	{
		L"./Resources/KittyCraft_01.dds",
		L"./Resources/KittyCraft_02.dds",
		L"./Resources/KittyCraft_03.dds",
		L"./Resources/KittyCraft_04.dds",
		L"./Resources/KittyCraft_05.dds",
		L"./Resources/KittyCraft_06.dds"
	};

	// Set meshes to the BasicMeshObject
	pMeshObj->BeginCreateMesh(vertices, numVertices, 6);	// 박스의 6면-1면당 삼각형 2개-인덱스 6개
	for (int i = 0; i < 6; i++)
	{
		pMeshObj->InsertTriGroup(indices + i * 6, 2, wchTexFileNameList[i]);
	}
	pMeshObj->EndCreateMesh();

	// delete vertices and indices
	if (vertices)
	{
		DeleteBoxMesh(vertices);
		vertices = nullptr;
	}
	return pMeshObj;
}

bool Game::Initialize(HWND hWnd, bool bEnableDebugLayer, bool bEnableGBV)
{
	// Load Renderer DLL
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

	// Initialize flecs
	m_ECSWorld = flecs::world();

	// Register components
	m_ECSWorld.component<Position>();
	m_ECSWorld.component<Velocity>();
	m_ECSWorld.component<Force>();
	m_ECSWorld.component<Rotation>();
	m_ECSWorld.component<Scale>();
	m_ECSWorld.component<MeshRenderer>();
	m_ECSWorld.component<SpriteRenderer>();

	// Create phases
	static flecs::entity phaseUpdate;
	static flecs::entity phasePhysics;
	static flecs::entity phaseBeginRender;
	static flecs::entity phaseRender;
	static flecs::entity phaseEndRender;

	flecs::entity_t phase = flecs::Phase;
	phaseUpdate = m_ECSWorld.entity("PhaseUpdate").add(phase);
	phasePhysics = m_ECSWorld.entity("PhasePhysics").add(phase);
	phaseBeginRender = m_ECSWorld.entity("PhaseBeginRender").add(flecs::Phase);
	phaseRender = m_ECSWorld.entity("PhaseRender").add(phase);
	phaseEndRender = m_ECSWorld.entity("PhaseEndRender").add(flecs::Phase);

	// Update -> Physics -> Render
	phasePhysics.add(flecs::DependsOn, phaseUpdate);
	phaseBeginRender.add(flecs::DependsOn, phasePhysics);
	phaseRender.add(flecs::DependsOn, phaseBeginRender);
	phaseEndRender.add(flecs::DependsOn, phaseRender);

	// Register systems
	m_ECSWorld.system<Position, Velocity, const Force>("Physics")
		.kind(phasePhysics)
		.each([](Position& p, Velocity& v, const Force& f)
			{
				const float dt = 1.0f / 60.0f;
				v.x += f.x * dt;
				v.y += f.y * dt;
				v.z += f.z * dt;
				p.x += v.x * dt;
				p.y += v.y * dt;
				p.z += v.z * dt;
			});

	m_ECSWorld.observer<MeshRenderer>("Init MeshRenderer")
		.event(flecs::OnSet)
		.each([this](MeshRenderer& m)
			{
				m.Mesh = createBoxMeshObject(m_pRenderer);
			});


	m_ECSWorld.system<>()
		.kind(phaseBeginRender)
		.each([this]() 
			{ 
				m_pRenderer->BeginRender(); 
			});

	m_ECSWorld.system<const Position, const Rotation, const Scale, const MeshRenderer>("Render Mesh")
		.kind(phaseRender)
		.multi_threaded(false)	// TODO: test multi threading
		.each([this](const Position& p, const Rotation& r, const Scale& s, const MeshRenderer& mesh)
			{
				DirectX::XMMATRIX matScale = DirectX::XMMatrixScaling(s.x, s.y, s.z);
				DirectX::XMMATRIX matRot = DirectX::XMMatrixRotationRollPitchYaw(r.Pitch, r.Yaw, r.Roll);
				DirectX::XMMATRIX matTrans = DirectX::XMMatrixTranslation(p.x, p.y, p.z);
				DirectX::XMMATRIX worldMat = matScale * matRot * matTrans;
				if (mesh.Mesh)
				{
					m_pRenderer->RenderMeshObject(mesh.Mesh, &worldMat);
				}
			});

	m_ECSWorld.system<>()
		.kind(phaseEndRender)
		.each([this]() 
			{
				m_pRenderer->EndRender();
				m_pRenderer->Present();
			});

	// Create Game Objects
	const UINT GAME_OBJ_COUNT = 200;
	for (UINT i = 0; i < GAME_OBJ_COUNT; i++)
	{
		float x = (float)((rand() % 51) - 25);	// -10m - 10m 
		float y = (float)((rand() % 3) - 1);	// -1m - 1m
		float z = (float)((rand() % 51) - 25);	// -10m - 10m 
		float r = (rand() % 181) * (3.1415f / 180.0f);
		float s = 0.5f * (float)((rand() % 10) + 1);	// 1 - 3
		float vx = (float)((rand() % 3) - 1);
		float vz = (float)((rand() % 3) - 1);

		flecs::entity e = m_ECSWorld.entity()
			.set<Position>({ x, y, z })
			.set<Velocity>({ vx, 0.0f, vz })
			.set<Force>({ 0.0f, 0.0f, 0.0f })
			.set<Rotation>({ 0.0f, r, 0.0f })
			.set<Scale>({ s, s, s })
			.set<MeshRenderer>({ nullptr });

		m_Entities.emplace_back(e.id());
	}

	// begin perf check
	LARGE_INTEGER prevCounter = QCGetCounter();

	// end perf check
	float elpasedTick = QCMeasureElapsedTick(QCGetCounter(), prevCounter);

	WCHAR wchTxt[128] = {};
	swprintf_s(wchTxt, L"App Initialized. GPU-UploadHeaps:%s, %.2f ms elapsed.\n", m_pRenderer->IsGpuUploadHeapsEnabled() ? L"Enabled" : L"N/A", elpasedTick);
	OutputDebugStringW(wchTxt);

	m_pRenderer->SetCameraPos(0.0f, 0.0f, -10.0f);

	return true;
}

void Game::Run()
{
	m_FrameCount++;

	uint64_t currTick = GetTickCount64();

	// game business logic
	Update(currTick);

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

	// Update ECS world
	float dt = (float)(currTick - m_PrevUpdateTick) * 0.001f;
	m_ECSWorld.progress(dt);

	return true;
}

void Game::Cleanup()
{
	m_ECSWorld.release();

	if (m_pRenderer)
	{
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