#include <iostream>
#include <format>
#include <filesystem>
#include <Windows.h>
#include <DirectXMath.h>
#include <shlwapi.h>

#include "Common/Common.h"
#include "Common/QueryPerfCounter.h"
#include "Interface/IRenderer.h"
#include "Game.h"
#include "Component.h"


// #define USE_GPU_UPLOAD_HEAPS

// TODO: Separate to another file or project
static IMeshObject* createBoxMeshObject(IRenderer* pRenderer, IGeometry* pGeometry)
{
	IMeshObject* pMeshObj = nullptr;
	const WCHAR* wchTexFileNameList[6] =
	{
		L"./Resources/KittyCraft_01.dds",
		L"./Resources/KittyCraft_02.dds",
		L"./Resources/KittyCraft_03.dds",
		L"./Resources/KittyCraft_04.dds",
		L"./Resources/KittyCraft_05.dds",
		L"./Resources/KittyCraft_06.dds"
	};
	// Create box mesh
	MeshData meshData = pGeometry->CreateUnitCubeMesh();

	// create BasicMeshObject from Renderer
	pMeshObj = pRenderer->CreateBasicMeshObject();
	pMeshObj->BeginCreateMesh(meshData.Vertices.data(), meshData.Vertices.size(), 6);	// 박스의 6면-1면당 삼각형 2개-인덱스 6개
	for (int i = 0; i < 6; i++)
	{
		pMeshObj->InsertTriGroup(meshData.Indices.data() + i * 6, 2, wchTexFileNameList[i]);
	}
	pMeshObj->EndCreateMesh();

	return pMeshObj;
}

static IMeshObject* createSphereMeshObject(IRenderer* pRenderer, IGeometry* pGeometry)
{
	IMeshObject* pMeshObj = nullptr;
	const WCHAR* wchTexFileName = L"./Resources/Kanna.dds";
	// Create sphere mesh
	MeshData meshData = pGeometry->CreateSphereMesh(1.0f, 20, 20);
	// create BasicMeshObject from Renderer
	pMeshObj = pRenderer->CreateBasicMeshObject();
	pMeshObj->BeginCreateMesh(meshData.Vertices.data(), meshData.Vertices.size(), 1);
	pMeshObj->InsertTriGroup(meshData.Indices.data(), meshData.Indices.size() / 3, wchTexFileName);
	pMeshObj->EndCreateMesh();

	return pMeshObj;
}

static IMeshObject* createPlaneMeshObject(IRenderer* pRenderer, IGeometry* pGeometry)
{
	IMeshObject* pMeshObj = nullptr;
	const WCHAR* wchTexFileName = L"./Resources/Floor.dds";
	// Create plane mesh
	MeshData meshData = pGeometry->CreatePlaneMesh(20.0f, 20.0f);
	// create BasicMeshObject from Renderer
	pMeshObj = pRenderer->CreateBasicMeshObject();
	pMeshObj->BeginCreateMesh(meshData.Vertices.data(), meshData.Vertices.size(), 1);
	pMeshObj->InsertTriGroup(meshData.Indices.data(), meshData.Indices.size() / 3, wchTexFileName);
	pMeshObj->EndCreateMesh();
	return pMeshObj;
}

static IMeshObject* createCylinderMeshObject(IRenderer* pRenderer, IGeometry* pGeometry)
{
	IMeshObject* pMeshObj = nullptr;
	const WCHAR* wchTexFileName = L"./Resources/Stone.dds";
	// Create cylinder mesh
	MeshData meshData = pGeometry->CreateCylinderMesh(0.5f, 2.0f, 20);
	// create BasicMeshObject from Renderer
	pMeshObj = pRenderer->CreateBasicMeshObject();
	pMeshObj->BeginCreateMesh(meshData.Vertices.data(), meshData.Vertices.size(), 1);
	pMeshObj->InsertTriGroup(meshData.Indices.data(), meshData.Indices.size() / 3, wchTexFileName);
	pMeshObj->EndCreateMesh();
	return pMeshObj;
}

static IMeshObject* createConeMeshObject(IRenderer* pRenderer, IGeometry* pGeometry)
{
	IMeshObject* pMeshObj = nullptr;
	const WCHAR* wchTexFileName = L"./Resources/Stone.dds";
	// Create torus mesh
	MeshData meshData = pGeometry->CreateConeMesh(1.0f, 2.0f, 20);
	// create BasicMeshObject from Renderer
	pMeshObj = pRenderer->CreateBasicMeshObject();
	pMeshObj->BeginCreateMesh(meshData.Vertices.data(), meshData.Vertices.size(), 1);
	pMeshObj->InsertTriGroup(meshData.Indices.data(), meshData.Indices.size() / 3, wchTexFileName);
	pMeshObj->EndCreateMesh();
	return pMeshObj;
}

static IMeshObject* createGridMeshObject(IRenderer* pRenderer, IGeometry* pGeometry)
{
	IMeshObject* pMeshObj = nullptr;
	const WCHAR* wchTexFileName = L"./Resources/Floor.dds";
	// Create grid mesh
	MeshData meshData = pGeometry->CreateGridMesh(20.0f, 20.0f, 3, 3);
	// create BasicMeshObject from Renderer
	pMeshObj = pRenderer->CreateBasicMeshObject();
	pMeshObj->BeginCreateMesh(meshData.Vertices.data(), meshData.Vertices.size(), 1);
	pMeshObj->InsertTriGroup(meshData.Indices.data(), meshData.Indices.size() / 3, wchTexFileName);
	pMeshObj->EndCreateMesh();
	return pMeshObj;
}

bool Game::Initialize(HWND hWnd, bool bEnableDebugLayer, bool bEnableGBV)
{
	// Load Renderer DLL
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
			ASSERT(false, "Fail to load Renderer DLL");
		}
		CREATE_INSTANCE_FUNC pCreateFunc = (CREATE_INSTANCE_FUNC)GetProcAddress(m_hRendererDLL, "DllCreateInstance");
		pCreateFunc(&m_pRenderer);
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
			MessageBox(hWnd, wchErrTxt, L"Error", MB_OK);
			ASSERT(false, "Fail to load Geometry DLL");
		}
		CREATE_INSTANCE_FUNC pCreateFunc = (CREATE_INSTANCE_FUNC)GetProcAddress(m_hGeometryDLL, "DllCreateInstance");
		pCreateFunc(&m_pGeometry);
	}

	// Get App Path and Set Shader Path
	//WCHAR exePath[_MAX_PATH] = {};
	WCHAR wchShaderPath[_MAX_PATH] = L"./Shaders";
	//if (GetModuleFileNameW(nullptr, exePath, _MAX_PATH))
	//{
	//	std::filesystem::path exeDir = std::filesystem::path(exePath).parent_path();         // ...\x64\Debug
	//	std::filesystem::path shaders = (exeDir / L"./Shaders").lexically_normal();

	//	wcsncpy_s(wchShaderPath, shaders.c_str(), _TRUNCATE);      // $(SolutionDir)\Shaders
	//	// wprintf(L"Shaders = %s\n", wchShaderPath);
	//}

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
	m_ECSWorld.component<TextRenderer>();

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

	// Initialize components when they are added
	{
		m_ECSWorld.observer<MeshRenderer>("Init MeshRenderer")
			.event(flecs::OnSet)
			.each([this](MeshRenderer& m)
				{

				});

		m_ECSWorld.observer<SpriteRenderer>("Init SpriteRenderer")
			.event(flecs::OnSet)
			.each([this](SpriteRenderer& s)
				{
					if (!s.SpriteFileName.empty())
					{
						s.Sprite = m_pRenderer->CreateSpriteObject(s.SpriteFileName.c_str());
					}
				});

		m_ECSWorld.observer<TextRenderer>("Init TextRenderer")
			.event(flecs::OnSet)
			.each([this](TextRenderer& t)
				{
					t.Width = 512;
					t.Height = 64;
					t.pImageData = (uint8_t*)malloc((size_t)t.Width * t.Height * 4);
					ASSERT(t.pImageData, "Fail to allocate memory for text image");
					memset(t.pImageData, 0, (size_t)t.Width * t.Height * 4);
					t.pTextTexHandle = m_pRenderer->CreateDynamicTexture(t.Width, t.Height);
					t.Sprite = m_pRenderer->CreateSpriteObject();
					t.pFontObject = m_pRenderer->CreateFontObject(L"Tahoma", 18.0f);
				});
	}
	// Register systems
	{
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

		m_ECSWorld.system<const Position, const Rotation, const Scale, const SpriteRenderer>("Render Sprite")
			.kind(phaseRender)
			.multi_threaded(false)	// TODO: test multi threading
			.each([this](const Position& p, const Rotation& r, const Scale& s, const SpriteRenderer& sprite)
				{
					ASSERT(sprite.Sprite, "SpriteRenderer. Sprite is null");
					if (sprite.Sprite)
					{
						m_pRenderer->RenderSprite(sprite.Sprite, (int)p.x, (int)p.y, s.x, s.y, 0.0f);
					}
				});

		m_ECSWorld.system<const Position, const Rotation, const Scale, TextRenderer>("Render Text")
			.kind(phaseRender)
			.multi_threaded(false)	// TODO: test multi threading
			.each([this](const Position& p, const Rotation& r, const Scale& s, TextRenderer& text)
				{
					ASSERT(text.Sprite, "TextRenderer. Sprite is null");
					if (!text.Text.empty())
					{
						memset(text.pImageData, 0, (size_t)text.Width * text.Height * 4);
						int outTextWidth = 0, outTexHeight = 0;
						m_pRenderer->WriteTextToBitmap(text.pImageData, text.Width, text.Height, text.Width * 4, &outTextWidth, &outTexHeight, text.pFontObject, text.Text.c_str(), (UINT)text.Text.length());
						text.Width = outTextWidth;
						text.Height = outTexHeight;
						m_pRenderer->UpdateTextureWithImage(text.pTextTexHandle, text.pImageData, text.Width, text.Height);
					}

					if (text.Sprite)
					{
						m_pRenderer->RenderSpriteWithTex(text.Sprite, (int)p.x, (int)p.y, s.x, s.y, nullptr, 0.0f, text.pTextTexHandle);
					}
				});

		m_ECSWorld.system<>()
			.kind(phaseEndRender)
			.each([this]()
				{
					m_pRenderer->EndRender();
					m_pRenderer->Present();
				});
	}
	// Cleanup when components are removed
	{
		m_ECSWorld.observer<MeshRenderer>()
			.event(flecs::OnRemove)
			.each([this](MeshRenderer& m)
				{
					if (m.Mesh)
					{
						m.Mesh->Release();
					}
				});

		m_ECSWorld.observer<SpriteRenderer>()
			.event(flecs::OnRemove)
			.each([this](SpriteRenderer& s)
				{
					if (s.Sprite)
					{
						s.Sprite->Release();
					}
				});

		m_ECSWorld.observer<TextRenderer>()
			.event(flecs::OnRemove)
			.each([this](TextRenderer& t)
				{
					if (t.pFontObject)
					{
						m_pRenderer->DeleteFontObject(t.pFontObject);
					}
					if (t.pTextTexHandle)
					{
						m_pRenderer->DeleteTexture(t.pTextTexHandle);
					}
					if (t.pImageData)
					{
						free(t.pImageData);
						t.pImageData = nullptr;
					}
					if (t.Sprite)
					{
						t.Sprite->Release();
					}
				});
	}

	// Create Game Objects
	{
		// Create plane
		{
			flecs::entity e = m_ECSWorld.entity()
				.set<Position>({ 0.0f, -5.0f, 0.0f })
				.set<Rotation>({ 0.0f, 0.0f, 0.0f })
				.set<Scale>({ 10.0f, 10.0f, 10.0f })
				.set<MeshRenderer>({ createPlaneMeshObject(m_pRenderer, m_pGeometry) });
			m_Entities.emplace_back(e.id());
		}
		// Create cylinder
		{
			flecs::entity e = m_ECSWorld.entity()
				.set<Position>({ -15.0f, 0.0f, 0.0f })
				.set<Rotation>({ 0.0f, 0.0f, 0.0f })
				.set<Scale>({ 3.0f, 3.0f, 3.0f })
				.set<MeshRenderer>({ createCylinderMeshObject(m_pRenderer, m_pGeometry) });
			m_Entities.emplace_back(e.id());
		}
		// Create cone
		{
			flecs::entity e = m_ECSWorld.entity()
				.set<Position>({ 15.0f, 0.0f, 0.0f })
				.set<Rotation>({ 0.0f, 0.0f, 0.0f })
				.set<Scale>({ 3.0f, 3.0f, 3.0f })
				.set<MeshRenderer>({ createConeMeshObject(m_pRenderer, m_pGeometry) });
			m_Entities.emplace_back(e.id());
		}
		// Create box entities
		const UINT BOX_OBJECT_COUNT = 50;
		for (UINT i = 0; i < BOX_OBJECT_COUNT; i++)
		{
			float x = (float)((rand() % 51) - 25);	// -10m - 10m 
			float y = (float)((rand() % 11) - 2);	// -2m - 7m
			float z = (float)((rand() % 51) - 25);	// -10m - 10m 
			float rx = (rand() % 181) * (3.1415f / 180.0f);
			float ry = (rand() % 181) * (3.1415f / 180.0f);
			float rz = (rand() % 181) * (3.1415f / 180.0f);
			float s = 0.5f * (float)((rand() % 10) + 1) * 0.5f;	// 1 - 3
			float vx = (float)((rand() % 3) - 1);
			float vz = (float)((rand() % 3) - 1);

			flecs::entity e = m_ECSWorld.entity()
				.set<Position>({ x, y, z })
				.set<Velocity>({ vx, 0.0f, vz })
				.set<Force>({ 0.0f, 0.0f, 0.0f })
				.set<Rotation>({ rx, ry, rz })
				.set<Scale>({ s, s, s })
				.set<MeshRenderer>({ createBoxMeshObject(m_pRenderer, m_pGeometry) });
			m_Entities.emplace_back(e.id());
		}
		// Create sphere entities
		const UINT SPHERE_OBJECT_COUNT = 30;
		for (UINT i = 0; i < SPHERE_OBJECT_COUNT; i++)
		{
			float x = (float)((rand() % 51) - 25);	// -10m - 10m 
			float y = (float)((rand() % 11) - 5);	// -5m - 5m
			float z = (float)((rand() % 51) - 25);	// -10m - 10m 
			float rx = (rand() % 181) * (3.1415f / 180.0f);
			float ry = (rand() % 181) * (3.1415f / 180.0f);
			float rz = (rand() % 181) * (3.1415f / 180.0f);
			float s = 0.5f * (float)((rand() % 10) + 1) * 0.5f;	// 1 - 3
			float vx = (float)((rand() % 3) - 1);
			float vz = (float)((rand() % 3) - 1);

			flecs::entity e = m_ECSWorld.entity()
				.set<Position>({ x, y, z })
				.set<Velocity>({ vx, 0.0f, vz })
				.set<Force>({ 0.0f, 0.0f, 0.0f })
				.set<Rotation>({ rx, ry, rz })
				.set<Scale>({ s, s, s })
				.set<MeshRenderer>({ createSphereMeshObject(m_pRenderer, m_pGeometry) });
			m_Entities.emplace_back(e.id());
		}

		// Create sprite entity
		{
			flecs::entity e = m_ECSWorld.entity()
				.set<Position>({ 100.0f, 100.0f, 0.0f })
				.set<Rotation>({ 0.0f, 0.0f, 0.0f })
				.set<Scale>({ 0.1f, 0.1f, 0.1f })
				.set<SpriteRenderer>({ L"./Resources/Kanna.dds" });

			m_Entities.emplace_back(e.id());
		}
		// Create text entity
		{
			flecs::entity e = m_ECSWorld.entity()
				.set<Position>({ 500.0f, 100.0f, 0.0f })
				.set<Rotation>({ 0.0f, 0.0f, 0.0f })
				.set<Scale>({ 1.0f, 1.0f, 1.0f })
				.set<TextRenderer>(TextRenderer(L"Hello"));

			m_Entities.emplace_back(e.id());
		}
	}

	// begin perf check
	LARGE_INTEGER prevCounter = QCGetCounter();

	// end perf check
	float elpasedTick = QCMeasureElapsedTick(QCGetCounter(), prevCounter);

	WCHAR wchTxt[128] = {};
	swprintf_s(wchTxt, L"App Initialized. GPU-UploadHeaps:%s, %.2f ms elapsed.\n", m_pRenderer->IsGpuUploadHeapsEnabled() ? L"Enabled" : L"N/A", elpasedTick);
	OutputDebugStringW(wchTxt);

	m_pRenderer->SetCameraPos(0.0f, 0.0f, 0.0f);

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