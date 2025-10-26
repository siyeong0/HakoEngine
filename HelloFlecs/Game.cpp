#include <iostream>
#include <format>
#include <filesystem>
#include <Windows.h>
#include <DirectXMath.h>
#include <shlwapi.h>

#include "Common/Common.h"
#include "Generic/QueryPerfCounter.h"
#include "Interface/IRenderer.h"
#include "Game.h"
#include "Component.h"


// #define USE_GPU_UPLOAD_HEAPS

// TODO: Separate to another file or project
static IMeshObject* createKittyBoxMeshObject(IRenderer* pRenderer)
{
	static IMeshObject* pMeshObj = nullptr; // TODO: static으로 관리하면 Release가 프로그램 종료 시점까지 안됨. RefCount 누수인것처럼 나옴. 따로 관리하도록..
	if (pMeshObj)
	{
		pMeshObj->AddRef();
		return pMeshObj;
	}

	const wchar_t* diffuseTexPaths[6] =
	{
		L"./Resources/KittyCraft_01.dds",
		L"./Resources/KittyCraft_02.dds",
		L"./Resources/KittyCraft_03.dds",
		L"./Resources/KittyCraft_04.dds",
		L"./Resources/KittyCraft_05.dds",
		L"./Resources/KittyCraft_06.dds"
	};

	StaticMesh meshData = StaticMesh::CreateUnitCubeMesh();
	std::vector<Vertex> vertices = meshData.GetVertexArray();

	pMeshObj = pRenderer->CreateBasicMeshObject();
	pMeshObj->BeginCreateMesh(vertices.data(), (uint)vertices.size(), 6);	// 박스의 6면-1면당 삼각형 2개-인덱스 6개
	for (int i = 0; i < 6; i++)
	{
		Material mtl(diffuseTexPaths[i], nullptr, nullptr, nullptr, nullptr, MATERIAL_TYPE_METAL);
		pMeshObj->InsertTriGroup(meshData.Sections[0].Indices.data() + i * 6, 2, mtl);
	}
	pMeshObj->EndCreateMesh();

	return pMeshObj;
}

static IMeshObject* createMegayuchiBoxMeshObject(IRenderer* pRenderer)
{
	static IMeshObject* pMeshObj = nullptr;
	if (pMeshObj)
	{
		pMeshObj->AddRef();
		return pMeshObj;
	}

	const wchar_t* diffuseTexPaths[6] =
	{
		L"./Resources/megayuchi/tex_00.dds",
		L"./Resources/megayuchi/tex_01.dds",
		L"./Resources/megayuchi/tex_02.dds",
		L"./Resources/megayuchi/tex_03.dds",
		L"./Resources/megayuchi/tex_04.dds",
		L"./Resources/megayuchi/tex_05.dds"
	};

	const wchar_t* normalTexPaths[6] =
	{
		L"./Resources/megayuchi/tex_00_N.dds",
		L"./Resources/megayuchi/tex_01_N.dds",
		L"./Resources/megayuchi/tex_02_N.dds",
		L"./Resources/megayuchi/tex_03_N.dds",
		L"./Resources/megayuchi/tex_04_N.dds",
		L"./Resources/megayuchi/tex_05_N.dds"
	};

	StaticMesh meshData = StaticMesh::CreateUnitCubeMesh();

	std::vector<Vertex> vertices = meshData.GetVertexArray();

	pMeshObj = pRenderer->CreateBasicMeshObject();
	pMeshObj->BeginCreateMesh(vertices.data(), (uint)vertices.size(), 6);	// 박스의 6면-1면당 삼각형 2개-인덱스 6개
	for (int i = 0; i < 6; i++)
	{
		Material mtl(diffuseTexPaths[i], normalTexPaths[i], nullptr, nullptr, nullptr, MATERIAL_TYPE_MATTE);
		pMeshObj->InsertTriGroup(meshData.Sections[0].Indices.data() + i * 6, 2, mtl);
	}
	pMeshObj->EndCreateMesh();
	return pMeshObj;
}

static IMeshObject* createKannaSphereMeshObject(IRenderer* pRenderer)
{
	static IMeshObject* pMeshObj = nullptr;
	if (pMeshObj)
	{
		pMeshObj->AddRef();
		return pMeshObj;
	}

	const wchar_t* diffuseTexPath = L"./Resources/Kanna.dds";

	StaticMesh meshData = StaticMesh::CreateSphereMesh(1.0f, 20, 20);

	std::vector<Vertex> vertices = meshData.GetVertexArray();

	pMeshObj = pRenderer->CreateBasicMeshObject();
	pMeshObj->BeginCreateMesh(vertices.data(), (uint)vertices.size(), (uint)meshData.Sections.size());
	for (const MeshSection& sec : meshData.Sections)
	{
		Material mtl(diffuseTexPath, nullptr, nullptr, nullptr, nullptr, MATERIAL_TYPE_MIRROR);
		pMeshObj->InsertTriGroup(sec.Indices.data(), (uint)(sec.Indices.size() / 3), mtl);
	}
	pMeshObj->EndCreateMesh();
	return pMeshObj;
}

static IMeshObject* createPlaneMeshObject(IRenderer* pRenderer)
{
	static IMeshObject* pMeshObj = nullptr;
	if (pMeshObj)
	{
		pMeshObj->AddRef();
		return pMeshObj;
	}

	const wchar_t* diffuseTexPath = L"./Resources/Floor.dds";

	StaticMesh meshData = StaticMesh::CreatePlaneMesh(20.0f, 20.0f);
	std::vector<Vertex> vertices = meshData.GetVertexArray();

	pMeshObj = pRenderer->CreateBasicMeshObject();
	pMeshObj->BeginCreateMesh(vertices.data(), (uint)vertices.size(), (uint)meshData.Sections.size());
	for (const MeshSection& sec : meshData.Sections)
	{
		Material mtl(diffuseTexPath, nullptr, nullptr, nullptr, nullptr, MATERIAL_TYPE_MIRROR);
		pMeshObj->InsertTriGroup(sec.Indices.data(), (uint)(sec.Indices.size() / 3), mtl);
	}
	pMeshObj->EndCreateMesh();
	return pMeshObj;
}

static IMeshObject* createStoneCylinderMeshObject(IRenderer* pRenderer)
{
	static IMeshObject* pMeshObj = nullptr;
	if (pMeshObj)
	{
		pMeshObj->AddRef();
		return pMeshObj;
	}

	const wchar_t* diffuseTexPath = L"./Resources/Stone.dds";

	StaticMesh meshData = StaticMesh::CreateCylinderMesh(0.5f, 2.0f, 20);
	std::vector<Vertex> vertices = meshData.GetVertexArray();

	pMeshObj = pRenderer->CreateBasicMeshObject();
	pMeshObj->BeginCreateMesh(vertices.data(), (uint)vertices.size(), (uint)meshData.Sections.size());
	for (const MeshSection& sec : meshData.Sections)
	{
		Material mtl(diffuseTexPath, nullptr, nullptr, nullptr, nullptr, MATERIAL_TYPE_MATTE);
		pMeshObj->InsertTriGroup(sec.Indices.data(), (uint)(sec.Indices.size() / 3), mtl);
	}
	pMeshObj->EndCreateMesh();
	return pMeshObj;
}

static IMeshObject* createStoneConeMeshObject(IRenderer* pRenderer)
{
	static IMeshObject* pMeshObj = nullptr;
	if (pMeshObj)
	{
		pMeshObj->AddRef();
		return pMeshObj;
	}

	const wchar_t* diffuseTexPath = L"./Resources/Stone.dds";

	StaticMesh meshData = StaticMesh::CreateConeMesh(1.0f, 2.0f, 20);
	std::vector<Vertex> vertices = meshData.GetVertexArray();

	pMeshObj = pRenderer->CreateBasicMeshObject();
	pMeshObj->BeginCreateMesh(vertices.data(), (uint)vertices.size(), (uint)meshData.Sections.size());
	for (const MeshSection& sec : meshData.Sections)
	{
		Material mtl(diffuseTexPath, nullptr, nullptr, nullptr, nullptr, MATERIAL_TYPE_MATTE);
		pMeshObj->InsertTriGroup(sec.Indices.data(), (uint)(sec.Indices.size() / 3), mtl);
	}
	pMeshObj->EndCreateMesh();
	return pMeshObj;
}

static IMeshObject* createMetalTileGridMeshObject(IRenderer* pRenderer)
{
	static IMeshObject* pMeshObj = nullptr;
	if (pMeshObj)
	{
		pMeshObj->AddRef();
		return pMeshObj;
	}

	const wchar_t* diffuseTexPath = L"./Resources/megayuchi/tilemap_008.dds";
	const wchar_t* normalTexPath = L"./Resources/megayuchi/tilemap_008_N.dds";

	StaticMesh meshData = StaticMesh::CreateGridMesh(20.0f, 20.0f, 100, 100);
	std::vector<Vertex> vertices = meshData.GetVertexArray();

	pMeshObj = pRenderer->CreateBasicMeshObject();
	pMeshObj->BeginCreateMesh(vertices.data(), (uint)vertices.size(), (uint)meshData.Sections.size());
	for (const MeshSection& sec : meshData.Sections)
	{
		Material mtl(diffuseTexPath, normalTexPath, nullptr, nullptr, nullptr, MATERIAL_TYPE_MATTE);
		mtl.NormalScale = 0.4f;
		pMeshObj->InsertTriGroup(sec.Indices.data(), (uint)(sec.Indices.size() / 3), mtl);
	}
	pMeshObj->EndCreateMesh();
	return pMeshObj;
}

static IMeshObject* createWaterMeshObject(IRenderer* pRenderer)
{
	static IMeshObject* pMeshObj = nullptr;
	if (pMeshObj)
	{
		pMeshObj->AddRef();
		return pMeshObj;
	}

	const wchar_t* diffuseTexPath = L"./Resources/megayuchi/Wat_S_Mo_000.dds";

	StaticMesh meshData = StaticMesh::CreatePlaneMesh(20.0f, 20.0f);
	std::vector<Vertex> vertices = meshData.GetVertexArray();

	pMeshObj = pRenderer->CreateBasicMeshObject();
	pMeshObj->BeginCreateMesh(vertices.data(), (uint)vertices.size(), (uint)meshData.Sections.size());
	for (const MeshSection& sec : meshData.Sections)
	{
		Material mtl(diffuseTexPath, nullptr, nullptr, nullptr, nullptr, MATERIAL_TYPE_WATER);
		pMeshObj->InsertTriGroup(sec.Indices.data(), (uint)(sec.Indices.size() / 3), mtl);
	}
	pMeshObj->EndCreateMesh();
	return pMeshObj;
}

static IMeshObject* createWireWall(IRenderer* pRenderer)
{
	static IMeshObject* pMeshObj = nullptr;
	if (pMeshObj)
	{
		pMeshObj->AddRef();
		return pMeshObj;
	}
	const wchar_t* diffuseTexPath = L"./Resources/wire_fence.dds";
	StaticMesh meshData = StaticMesh::CreateGridMesh(1.0f, 1.0f, 5, 5);
	std::vector<Vertex> vertices = meshData.GetVertexArray();
	pMeshObj = pRenderer->CreateBasicMeshObject();
	pMeshObj->BeginCreateMesh(vertices.data(), (uint)vertices.size(), (uint)meshData.Sections.size());
	for (const MeshSection& sec : meshData.Sections)
	{
		Material mtl(diffuseTexPath, nullptr, nullptr, nullptr, nullptr, MATERIAL_TYPE_METAL, true);
		// mtl.RoughnessFactor = 0.8f;
		pMeshObj->InsertTriGroup(sec.Indices.data(), (uint)(sec.Indices.size() / 3), mtl);
	}
	pMeshObj->EndCreateMesh();
	return pMeshObj;
}

static IMeshObject* createMeshFromFile(IRenderer* pRenderer, const char* filename)
{
	static IMeshObject* pMeshObj = nullptr;
	if (pMeshObj)
	{
		pMeshObj->AddRef();
		return pMeshObj;
	}

	StaticMesh meshData;
	if (!meshData.LoadFromFile(filename, 10.0f))
	{
		std::cout << std::format("Failed to load model from file: {}\n", filename);
		return nullptr;
	}

	std::vector<Vertex> vertices = meshData.GetVertexArray();
	pMeshObj = pRenderer->CreateBasicMeshObject();
	pMeshObj->BeginCreateMesh(vertices.data(), (uint)vertices.size(), (uint)meshData.Sections.size());
	for (const MeshSection& sec : meshData.Sections)
	{
		pMeshObj->InsertTriGroup(sec.Indices.data(), (uint)(sec.Indices.size() / 3), sec.Material);
	}
	pMeshObj->EndCreateMesh();
	return pMeshObj;
}

static IProceduralSphereObject* createProceduralSphereObject(IRenderer* pRenderer, FLOAT3 center, float radius)
{
	IProceduralSphereObject* pSphereObj = pRenderer->CreateProceduralSphereObject();
	Material mtl(nullptr, nullptr, nullptr, nullptr, nullptr, MATERIAL_TYPE_MIRROR, false);
	pSphereObj->BeginCreateGeom(2, mtl);
	Sphere s1 = { center, radius };
	Sphere s2 = { center + FLOAT3{3.0f, 0.0f, 0.0f}, radius };
	pSphereObj->InsertSphere(s1);
	pSphereObj->InsertSphere(s2);
	pSphereObj->EndCreateGeom();
	return pSphereObj;
}

bool Game::Initialize(
	HWND hWnd,
	bool bEnableRayTracing,
	bool bEnableDebugLayer,
	bool bEnableGBV,
	bool bEnableShaderDebug)
{
	// Load Renderer DLL
	{
		const wchar_t* wchRendererFileName = nullptr;
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
		wchar_t wchErrTxt[128] = {};
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

	// Get App Path and Set Shader Path
	//wchar_t exePath[_MAX_PATH] = {};
	wchar_t wchShaderPath[_MAX_PATH] = L"./Shaders";
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

	m_pRenderer->Initialize(hWnd, bEnableRayTracing, bEnableDebugLayer, bEnableGBV, bEnableShaderDebug, bUseGpuUploadHeaps, wchShaderPath);
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

		m_ECSWorld.observer<ProceduralSphereRenderer>("Init ProceduralSphereRenderer")
			.event(flecs::OnSet)
			.each([this](ProceduralSphereRenderer& m)
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
					float dt = m_ECSWorld.delta_time();
					m_pRenderer->Update(dt);	// TODO: 빼도 되나?
					m_pRenderer->BeginRender();
				});

		m_ECSWorld.system<const Position, const Rotation, const Scale, const MeshRenderer>("Render Mesh")
			.kind(phaseRender)
			.multi_threaded(false)	// TODO: test multi threading
			.each([this](const Position& p, const Rotation& r, const Scale& s, const MeshRenderer& mesh)
				{
					Matrix4x4 matScale = DirectX::XMMatrixScaling(s.x, s.y, s.z);
					Matrix4x4 matRot = DirectX::XMMatrixRotationRollPitchYaw(r.Pitch, r.Yaw, r.Roll);
					Matrix4x4 matTrans = DirectX::XMMatrixTranslation(p.x, p.y, p.z);
					Matrix4x4 worldMat = matScale * matRot * matTrans;
					if (mesh.Mesh)
					{
						m_pRenderer->RenderMeshObject(mesh.Mesh, &worldMat);
					}
				});

		m_ECSWorld.system<const Position, const Rotation, const Scale, const ProceduralSphereRenderer>("Render Procedural")
			.kind(phaseRender)
			.multi_threaded(false)	// TODO: test multi threading
			.each([this](const Position& p, const Rotation& r, const Scale& s, const ProceduralSphereRenderer& proc)
				{
					// Matrix4x4 matScale = DirectX::XMMatrixScaling(s.x, s.y, s.z);
					Matrix4x4 matScale = DirectX::XMMatrixIdentity(); // Procedural sphere already has radius
					Matrix4x4 matRot = DirectX::XMMatrixRotationRollPitchYaw(r.Pitch, r.Yaw, r.Roll);
					Matrix4x4 matTrans = DirectX::XMMatrixTranslation(p.x, p.y, p.z);
					Matrix4x4 worldMat = matScale * matRot * matTrans;
					if (proc.Sphere)
					{
						m_pRenderer->RenderProceduralSphereObject(proc.Sphere, &worldMat);
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
						m_pRenderer->WriteTextToBitmap(text.pImageData, text.Width, text.Height, text.Width * 4, &outTextWidth, &outTexHeight, text.pFontObject, text.Text.c_str(), (uint)text.Text.length());
						text.Width = outTextWidth;
						text.Height = outTexHeight;
						m_pRenderer->UpdateTextureWithImage(text.pTextTexHandle, text.pImageData, text.Width, text.Height);
					}

					if (text.Sprite)
					{
						m_pRenderer->RenderSpriteWithTex(
							text.Sprite,
							(int)p.x, (int)p.y,
							s.x, s.y,
							nullptr, 0.0f,
							text.pTextTexHandle);
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
					SAFE_RELEASE(m.Mesh);
				});

		m_ECSWorld.observer<SpriteRenderer>()
			.event(flecs::OnRemove)
			.each([this](SpriteRenderer& s)
				{
					SAFE_RELEASE(s.Sprite);
				});

		m_ECSWorld.observer<TextRenderer>()
			.event(flecs::OnRemove)
			.each([this](TextRenderer& t)
				{
					if (t.pFontObject)
					{
						SAFE_CLEANUP(t.pFontObject, m_pRenderer->DeleteFontObject);
					}
					if (t.pTextTexHandle)
					{
						SAFE_CLEANUP(t.pTextTexHandle, m_pRenderer->DeleteTexture);
					}
					if (t.pImageData)
					{
						SAFE_FREE(t.pImageData);
					}
					if (t.Sprite)
					{
						SAFE_RELEASE(t.Sprite);
					}
				});
	}

	// Create Game Objects
	{
		// Create grid
		{
			flecs::entity e = m_ECSWorld.entity()
				.set<Position>({ 0.0f, -5.0f, 0.0f })
				.set<Rotation>({ 0.0f, 0.0f, 0.0f })
				.set<Scale>({ 100.0f, 100.0f, 100.0f })
				.set<MeshRenderer>({ createMetalTileGridMeshObject(m_pRenderer) });
			m_Entities.emplace_back(e.id());
		}
		// wire fence
		{
			flecs::entity e = m_ECSWorld.entity()
				.set<Position>({ 0.0f, 0.0f, 25.0f })
				.set<Rotation>({ PI / 2.0f, 0.0f, 0.0f })
				.set<Scale>({ 10.0f, 10.0f, 10.0f })
				.set<MeshRenderer>({ createWireWall(m_pRenderer) });
			m_Entities.emplace_back(e.id());
		}
		// Create kitty box entities
		const uint KITTY_BOX_OBJECT_COUNT = 0;
		for (uint i = 0; i < KITTY_BOX_OBJECT_COUNT; i++)
		{
			float x = (float)((rand() % 41) - 20);
			float y = (float)((rand() % 7) - 3);
			float z = (float)((rand() % 41) - 20);
			float rx = DegToRad(static_cast<float>(rand() % 181));
			float ry = DegToRad(static_cast<float>(rand() % 181));
			float rz = DegToRad(static_cast<float>(rand() % 181));
			float s = (float)((rand() % 4) + 1) * 0.5f;	// 1 - 3
			float vx = (float)((rand() % 3) - 1);
			float vz = (float)((rand() % 3) - 1);

			flecs::entity e = m_ECSWorld.entity()
				.set<Position>({ x, y, z })
				.set<Velocity>({ vx, 0.0f, vz })
				.set<Force>({ 0.0f, 0.0f, 0.0f })
				.set<Rotation>({ rx, ry, rz })
				.set<Scale>({ s, s, s })
				.set<MeshRenderer>({ createKittyBoxMeshObject(m_pRenderer) });
			m_Entities.emplace_back(e.id());
		}
		// Create megayuchi box entities
		const uint MEGA_BOX_OBJECT_COUNT = 0;
		for (uint i = 0; i < MEGA_BOX_OBJECT_COUNT; i++)
		{
			float x = (float)((rand() % 41) - 20);
			float y = (float)((rand() % 7) - 3);
			float z = (float)((rand() % 41) - 20);
			float rx = DegToRad(static_cast<float>(rand() % 181));
			float ry = DegToRad(static_cast<float>(rand() % 181));
			float rz = DegToRad(static_cast<float>(rand() % 181));
			float s = (float)((rand() % 4) + 1) * 0.5f;	// 1 - 3
			// float vx = (float)((rand() % 3) - 1);
			// float vz = (float)((rand() % 3) - 1);

			flecs::entity e = m_ECSWorld.entity()
				.set<Position>({ x, y, z })
				// .set<Velocity>({ vx, 0.0f, vz })
				.set<Force>({ 0.0f, 0.0f, 0.0f })
				.set<Rotation>({ rx, ry, rz })
				.set<Scale>({ s, s, s })
				.set<MeshRenderer>({ createMegayuchiBoxMeshObject(m_pRenderer) });
			m_Entities.emplace_back(e.id());
		}
		// Create sphere entities
		const uint SPHERE_OBJECT_COUNT = 1;
		for (uint i = 0; i < SPHERE_OBJECT_COUNT; i++)
		{
			float x = (float)((rand() % 41) - 20);
			float y = (float)((rand() % 7) - 3);
			float z = (float)((rand() % 41) - 20);
			float rx = DegToRad(static_cast<float>(rand() % 181));
			float ry = DegToRad(static_cast<float>(rand() % 181));
			float rz = DegToRad(static_cast<float>(rand() % 181));
			float s = (float)((rand() % 4) + 1) * 0.5f;
			float vx = (float)((rand() % 3) - 1);
			float vz = (float)((rand() % 3) - 1);

			flecs::entity e = m_ECSWorld.entity()
				.set<Position>({ x, y, z })
				.set<Velocity>({ vx, 0.0f, vz })
				.set<Force>({ 0.0f, 0.0f, 0.0f })
				.set<Rotation>({ rx, ry, rz })
				.set<Scale>({ 1.0f, 1.0f, 1.0f })
				.set<MeshRenderer>({ createKannaSphereMeshObject(m_pRenderer) });
			m_Entities.emplace_back(e.id());
		}

		// Create procedural sphere entities
		const uint PROC_SPHERE_OBJECT_COUNT = 1;
		for (uint i = 0; i < PROC_SPHERE_OBJECT_COUNT; i++)
		{
			float x = (float)((rand() % 41) - 20);
			float y = (float)((rand() % 7) - 3);
			float z = (float)((rand() % 41) - 20);
			float rx = DegToRad(static_cast<float>(rand() % 181));
			float ry = DegToRad(static_cast<float>(rand() % 181));
			float rz = DegToRad(static_cast<float>(rand() % 181));
			float vx = (float)((rand() % 3) - 1);
			float vz = (float)((rand() % 3) - 1);
			float s = (float)((rand() % 4) + 1) * 0.5f;
			float cx = (float)((rand() % 5) - 2);
			float cy = (float)((rand() % 5) - 2);
			float cz = (float)((rand() % 5) - 2);
			float radius = (float)((rand() % 10) + 1);

			flecs::entity e = m_ECSWorld.entity()
				.set<Position>({ x, y, z })
				// .set<Velocity>({ vx, 0.0f, vz })
				// .set<Force>({ 0.0f, 0.0f, 0.0f })
				.set<Rotation>({ rx, ry, rz })
				.set<Scale>({ 2, 2, 2 })
				.set<ProceduralSphereRenderer>({ createProceduralSphereObject(m_pRenderer, {cx, cy, cz}, radius) });
			m_Entities.emplace_back(e.id());
		}

		// Create from file (bunny)
		//{
		//	flecs::entity e = m_ECSWorld.entity()
		//		.set<Position>({ 15.0f, 10.0f, 0.0f })
		//		.set<Rotation>({ 0.0f, 0.0f, 0.0f })
		//		.set<Scale>({ 3.0f, 3.0f, 3.0f })
		//		.set<MeshRenderer>({ createMeshFromFile(m_pRenderer, "./Resources/Decomp/bunny.off") });
		//	m_Entities.emplace_back(e.id());
		//}

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

	wchar_t wchTxt[128] = {};
	swprintf_s(wchTxt, L"App Initialized. GPU-UploadHeaps:%s, %.2f ms elapsed.\n", m_pRenderer->IsGpuUploadHeapsEnabled() ? L"Enabled" : L"N/A", elpasedTick);
	OutputDebugStringW(wchTxt);

	m_pRenderer->SetCameraPos(0.0f, 0.0f, 0.0f);

	return true;
}

void Game::Run()
{
	uint64_t currTick = GetTickCount64();

	// game business logic
	if (Update(currTick))
	{
		++m_FrameCount;
	}

	if (currTick - m_PrevFrameCheckTick > 1000)
	{
		m_PrevFrameCheckTick = currTick;

		wchar_t wchTxt[64];
		m_FPS = m_FrameCount;
		m_NumCommandLists = m_pRenderer->GetCommandListCount();

		swprintf_s(wchTxt, L"FPS : %u, CommandList : %u ", m_FPS, m_NumCommandLists);
		SetWindowText(m_hWnd, wchTxt);

		m_FrameCount = 0;
	}
}

static FLOAT3 ComputeSunDir(
	float timeOfDayHours,
	float latitudeDeg = 37.6f,     // 서울 근처
	float declinationDeg = 15.0f)  // 여름 느낌: 15°, 겨울:  -15° 정도
{
	// 1) 시각경도 H (정오=0°, 시간당 15°)
	float H = DegToRad(15.0f * (timeOfDayHours - 12.0f));

	// 2) 고도/방위 계산
	float phi = DegToRad(latitudeDeg);
	float dec = DegToRad(declinationDeg);

	float sinAlt = std::sin(phi) * std::sin(dec) + std::cos(phi) * std::cos(dec) * std::cos(H);
	float alt = std::asin(std::clamp(sinAlt, -1.0f, 1.0f)); // 태양 고도(라디안)

	// 방위: 0=북(+Z), 시계방향으로 증가(동=90°, 남=180°, 서=270°)
	float cosAz = (std::sin(dec) - std::sin(alt) * std::sin(phi)) / (std::cos(alt) * std::cos(phi) + 1e-6f);
	cosAz = std::clamp(cosAz, -1.0f, 1.0f);
	float az = std::acos(cosAz);
	if (std::sin(H) > 0.0f) az = 2.0f * 3.14159265358979f - az; // 정오 이후 서쪽으로 이동

	// 3) 구면→직교 (+Y=Up, +Z=North, +X=East)
	float cosAlt = std::cos(alt);
	FLOAT3 d;
	d.x = cosAlt * std::sin(az); // East
	d.y = std::sin(alt);         // Up
	d.z = cosAlt * std::cos(az); // North
	return FLOAT3::Normalize(d);
}


bool Game::Update(uint64_t currTick)
{
	const uint64_t elapsedMs = currTick - m_PrevUpdateTick;
	const float dt = (float)elapsedMs * 0.001f / 3600.0f; // ms -> h
	m_PrevUpdateTick = currTick;

	if (m_CamOffsetX || m_CamOffsetY || m_CamOffsetZ)
	{
		m_pRenderer->MoveCamera(m_CamOffsetX, m_CamOffsetY, m_CamOffsetZ);
	}

	m_ECSWorld.progress(dt);

	m_TimeOfDay += dt * 10.0f;
	m_TimeOfDay = fmodf(m_TimeOfDay, 24.0f); // 0 - 24 hours

	FLOAT3 sunDir = ComputeSunDir(m_TimeOfDay, /*latitude*/37.6f, /*declination*/15.0f);

	m_pRenderer->SetSunDir(-sunDir);
	std::cout << m_TimeOfDay << std::endl;
	return true;
}

void Game::Cleanup()
{
	m_ECSWorld.release();

	SAFE_RELEASE(m_pGeometry);
	SAFE_FREE_LIBRARY(m_hGeometryDLL);

	SAFE_RELEASE(m_pRenderer);
	SAFE_FREE_LIBRARY(m_hRendererDLL);
}

void Game::OnKeyDown(uint nChar, uint uiScanCode)
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
	case 'K':
		m_TimeOfDay += 0.5f;
		std::cout << "Current Time of Day: " << m_TimeOfDay << " hours\n";
		break;
	}
}

void Game::OnKeyUp(uint nChar, uint uiScanCode)
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

void Game::OnMouseLButtonDown(int x, int y, uint nFlags)
{
	m_bMouseLButtonDown = true;
}

void Game::OnMouseLButtonUp(int x, int y, uint nFlags)
{
	m_bMouseLButtonDown = false;
}

void Game::OnMouseRButtonDown(int x, int y, uint nFlags)
{
	m_bCamRotMode = true;
	m_MouseXRButtonPressed = x;
	m_MouseYRButtonPressed = y;

	m_bMouseRButtonDown = true;
}

void Game::OnMouseRButtonUp(int x, int y, uint nFlags)
{
	m_bCamRotMode = false;
	m_bMouseRButtonDown = false;
}

void Game::OnMouseMButtonDown(int x, int y, uint nFlags)
{
	m_bMouseMButtonDown = true;
}

void Game::OnMouseMButtonUp(int x, int y, uint nFlags)
{
	m_bMouseMButtonDown = false;
}

void Game::OnMouseMove(int x, int y, uint nFlags)
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

bool Game::UpdateWindowSize(uint backBufferWidth, uint backBufferHeight)
{
	bool bResult = false;
	if (m_pRenderer)
	{
		bResult = m_pRenderer->UpdateWindowSize(backBufferWidth, backBufferHeight);
	}
	return bResult;
}