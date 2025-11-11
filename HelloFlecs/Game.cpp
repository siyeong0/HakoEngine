#include <iostream>
#include <format>
#include <filesystem>
#include <fstream>
#include <Windows.h>
#include <DirectXMath.h>
#include <shlwapi.h>

#include "Common/Common.h"
#include "Generic/QueryPerfCounter.h"
#include "Interface/IRenderer.h"
#include "Geometry/Volume.h"
#include "Geometry/TraceVolume.h"
#include "Game.h"
#include "HakoFlecs/Components.h"

// #define USE_GPU_UPLOAD_HEAPS

// TODO: Separate to another file or project

static IMeshObject* createCubeMeshObject(IRenderer* pRenderer)
{
	static IMeshObject* pMeshObj = nullptr;
	if (pMeshObj)
	{
		pMeshObj->AddRef();
		return pMeshObj;
	}
	UStaticMesh meshData = UStaticMesh::CreateUnitCubeMesh();
	std::vector<Vertex> vertices = meshData.GetVertexArray();
	pMeshObj = pRenderer->CreateBasicMeshObject();
	pMeshObj->BeginCreateMesh(vertices.data(), (uint)vertices.size(), (uint)meshData.Sections.size());
	for (const UMeshSection& sec : meshData.Sections)
	{
		UMaterial mtl(nullptr, nullptr, nullptr, nullptr, nullptr, MATERIAL_TYPE_MATTE);
		pMeshObj->InsertTriGroup(sec.Indices.data(), (uint)(sec.Indices.size() / 3), mtl);
	}
	pMeshObj->EndCreateMesh();
	return pMeshObj;
}

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
		L"./Resources/KittyCraft_01.png",
		L"./Resources/KittyCraft_02.dds",
		L"./Resources/KittyCraft_03.dds",
		L"./Resources/KittyCraft_04.dds",
		L"./Resources/KittyCraft_05.dds",
		L"./Resources/KittyCraft_06.dds"
	};

	UStaticMesh meshData = UStaticMesh::CreateUnitCubeMesh();
	std::vector<Vertex> vertices = meshData.GetVertexArray();

	pMeshObj = pRenderer->CreateBasicMeshObject();
	pMeshObj->BeginCreateMesh(vertices.data(), (uint)vertices.size(), 6);	// 박스의 6면-1면당 삼각형 2개-인덱스 6개
	for (int i = 0; i < 6; i++)
	{
		UMaterial mtl(diffuseTexPaths[i], nullptr, nullptr, nullptr, nullptr, MATERIAL_TYPE_METAL);
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

	UStaticMesh meshData = UStaticMesh::CreateUnitCubeMesh();

	std::vector<Vertex> vertices = meshData.GetVertexArray();

	pMeshObj = pRenderer->CreateBasicMeshObject();
	pMeshObj->BeginCreateMesh(vertices.data(), (uint)vertices.size(), 6);	// 박스의 6면-1면당 삼각형 2개-인덱스 6개
	for (int i = 0; i < 6; i++)
	{
		UMaterial mtl(diffuseTexPaths[i], normalTexPaths[i], nullptr, nullptr, nullptr, MATERIAL_TYPE_MATTE);
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

	UStaticMesh meshData = UStaticMesh::CreateSphereMesh(1.0f, 20, 20);

	std::vector<Vertex> vertices = meshData.GetVertexArray();

	pMeshObj = pRenderer->CreateBasicMeshObject();
	pMeshObj->BeginCreateMesh(vertices.data(), (uint)vertices.size(), (uint)meshData.Sections.size());
	for (const UMeshSection& sec : meshData.Sections)
	{
		UMaterial mtl(diffuseTexPath, nullptr, nullptr, nullptr, nullptr, MATERIAL_TYPE_MIRROR);
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

	UStaticMesh meshData = UStaticMesh::CreatePlaneMesh(20.0f, 20.0f);
	std::vector<Vertex> vertices = meshData.GetVertexArray();

	pMeshObj = pRenderer->CreateBasicMeshObject();
	pMeshObj->BeginCreateMesh(vertices.data(), (uint)vertices.size(), (uint)meshData.Sections.size());
	for (const UMeshSection& sec : meshData.Sections)
	{
		UMaterial mtl(diffuseTexPath, nullptr, nullptr, nullptr, nullptr, MATERIAL_TYPE_MIRROR);
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

	UStaticMesh meshData = UStaticMesh::CreateCylinderMesh(0.5f, 2.0f, 20);
	std::vector<Vertex> vertices = meshData.GetVertexArray();

	pMeshObj = pRenderer->CreateBasicMeshObject();
	pMeshObj->BeginCreateMesh(vertices.data(), (uint)vertices.size(), (uint)meshData.Sections.size());
	for (const UMeshSection& sec : meshData.Sections)
	{
		UMaterial mtl(diffuseTexPath, nullptr, nullptr, nullptr, nullptr, MATERIAL_TYPE_MATTE);
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

	UStaticMesh meshData = UStaticMesh::CreateConeMesh(1.0f, 2.0f, 20);
	std::vector<Vertex> vertices = meshData.GetVertexArray();

	pMeshObj = pRenderer->CreateBasicMeshObject();
	pMeshObj->BeginCreateMesh(vertices.data(), (uint)vertices.size(), (uint)meshData.Sections.size());
	for (const UMeshSection& sec : meshData.Sections)
	{
		UMaterial mtl(diffuseTexPath, nullptr, nullptr, nullptr, nullptr, MATERIAL_TYPE_MATTE);
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

	UStaticMesh meshData = UStaticMesh::CreateGridMesh(20.0f, 20.0f, 100, 100);
	std::vector<Vertex> vertices = meshData.GetVertexArray();

	pMeshObj = pRenderer->CreateBasicMeshObject();
	pMeshObj->BeginCreateMesh(vertices.data(), (uint)vertices.size(), (uint)meshData.Sections.size());
	for (const UMeshSection& sec : meshData.Sections)
	{
		UMaterial mtl(diffuseTexPath, normalTexPath, nullptr, nullptr, nullptr, MATERIAL_TYPE_MATTE);
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

	UStaticMesh meshData = UStaticMesh::CreatePlaneMesh(20.0f, 20.0f);
	std::vector<Vertex> vertices = meshData.GetVertexArray();

	pMeshObj = pRenderer->CreateBasicMeshObject();
	pMeshObj->BeginCreateMesh(vertices.data(), (uint)vertices.size(), (uint)meshData.Sections.size());
	for (const UMeshSection& sec : meshData.Sections)
	{
		UMaterial mtl(diffuseTexPath, nullptr, nullptr, nullptr, nullptr, MATERIAL_TYPE_WATER);
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
	UStaticMesh meshData = UStaticMesh::CreateGridMesh(1.0f, 1.0f, 5, 5);
	std::vector<Vertex> vertices = meshData.GetVertexArray();
	pMeshObj = pRenderer->CreateBasicMeshObject();
	pMeshObj->BeginCreateMesh(vertices.data(), (uint)vertices.size(), (uint)meshData.Sections.size());
	for (const UMeshSection& sec : meshData.Sections)
	{
		UMaterial mtl(diffuseTexPath, nullptr, nullptr, nullptr, nullptr, MATERIAL_TYPE_METAL, true);
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

	UStaticMesh meshData;
	if (!meshData.LoadFromFile(filename, 1.0f))
	{
		std::cout << std::format("Failed to load model from file: {}\n", filename);
		return nullptr;
	}
	// meshData.FlipYAxis();

	std::vector<Vertex> vertices = meshData.GetVertexArray();
	pMeshObj = pRenderer->CreateBasicMeshObject();
	pMeshObj->BeginCreateMesh(vertices.data(), (uint)vertices.size(), (uint)meshData.Sections.size());
	for (UMeshSection& sec : meshData.Sections)
	{
		sec.Material.DiffuseTexturePath = L"";
		pMeshObj->InsertTriGroup(sec.Indices.data(), (uint)(sec.Indices.size() / 3), sec.Material);
	}
	pMeshObj->EndCreateMesh();
	return pMeshObj;
}

static IProceduralSphereObject* createProceduralSphereObject(IRenderer* pRenderer, FLOAT3 center, float radius)
{
	IProceduralSphereObject* pSphereObj = pRenderer->CreateProceduralSphereObject();
	UMaterial mtl(nullptr, nullptr, nullptr, nullptr, nullptr, MATERIAL_TYPE_MIRROR, false);
	pSphereObj->BeginCreateGeom(2, mtl);
	Sphere s1 = { center, radius };
	Sphere s2 = { center + FLOAT3{3.0f, 0.0f, 0.0f}, radius };
	pSphereObj->InsertSphere(s1);
	pSphereObj->InsertSphere(s2);
	pSphereObj->EndCreateGeom();
	return pSphereObj;
}

static ICasperObject* createCasperObject(IRenderer* pRenderer, const std::wstring& path)
{
	const std::wstring metadataPath = path + L"/metadata.txt";
	const std::wstring casperDiffusePath = path + L"/casper_color.dds";
	const std::wstring casperDepthPath = path + L"/casper_depth.dds";

	std::ifstream ifs(metadataPath);
	ASSERT(ifs.is_open(), "Failed to open casper metadata file");
	Bounds casperBounds;
	std::string line;
	while (std::getline(ifs, line))
	{
		std::istringstream iss(line);
		std::string key;
		if (!(iss >> key))
			continue;
		if (key == "Min")
		{
			float x, y, z;
			if (iss >> x >> y >> z)
			{
				casperBounds.Min = FLOAT3(x, y, z);
			}
		}
		else if (key == "Max")
		{
			float x, y, z;
			if (iss >> x >> y >> z)
			{
				casperBounds.Max = FLOAT3(x, y, z);
			}
		}
	}

	float maxLength = std::max({ casperBounds.Max.x - casperBounds.Min.x,
		casperBounds.Max.y - casperBounds.Min.y,
		casperBounds.Max.z - casperBounds.Min.z });

	ICasperObject* pCasperObj = pRenderer->CreateCasperObject();
	UMaterial mtl(nullptr, nullptr, nullptr, nullptr, nullptr, MATERIAL_TYPE_DEFAULT, false);
	pCasperObj->BeginCreateCasper(1, mtl);
	CasperAtlas atlas = {};
	atlas.AtlasBounds = casperBounds;
	// atlas.AtlasBounds = Bounds(casperBounds.Min, casperBounds.Min + FLOAT3(maxLength, maxLength, maxLength));
	// atlas.AtlasBounds = Bounds(casperBounds.Max - FLOAT3(maxLength, maxLength, maxLength), casperBounds.Max);
	atlas.DiffuseAtlas = casperDiffusePath;
	atlas.DepthAtlas = casperDepthPath;
	pCasperObj->InsertCasperAtlas(atlas);
	pCasperObj->EndCreateCasper(true);
	return pCasperObj;
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
	m_ECSWorld.component<comp::Position>();
	m_ECSWorld.component<comp::Rotation>();
	m_ECSWorld.component<comp::Scale>();
	m_ECSWorld.component<comp::Transform>();
	m_ECSWorld.component<comp::RigidBody>();
	m_ECSWorld.component<comp::MeshRenderer>();
	m_ECSWorld.component<comp::ProceduralSphereRenderer>();
	m_ECSWorld.component<comp::CasperRenderer>();
	m_ECSWorld.component<comp::SpriteRenderer>();
	m_ECSWorld.component<comp::TextRenderer>();

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
		m_ECSWorld.observer<comp::MeshRenderer>("Init MeshRenderer")
			.event(flecs::OnSet)
			.each([this](comp::MeshRenderer& m)
				{

				});

		m_ECSWorld.observer<comp::ProceduralSphereRenderer>("Init ProceduralSphereRenderer")
			.event(flecs::OnSet)
			.each([this](comp::ProceduralSphereRenderer& m)
				{

				});

		m_ECSWorld.observer<comp::CasperRenderer>("Init CasperRenderer")
			.event(flecs::OnSet)
			.each([this](comp::CasperRenderer& m)
				{

				});

		m_ECSWorld.observer<comp::SpriteRenderer>("Init SpriteRenderer")
			.event(flecs::OnSet)
			.each([this](comp::SpriteRenderer& s)
				{
					if (!s.SpriteFileName.empty())
					{
						s.Sprite = m_pRenderer->CreateSpriteObject(s.SpriteFileName.c_str());
					}
				});

		m_ECSWorld.observer<comp::TextRenderer>("Init TextRenderer")
			.event(flecs::OnSet)
			.each([this](comp::TextRenderer& t)
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
		m_ECSWorld
			.system<comp::Transform, const comp::Position, const comp::Rotation, const comp::Scale>("Transform.Update")
			.kind(phaseUpdate)
			.each([](
				comp::Transform& t,
				const comp::Position& p,
				const comp::Rotation& r,
				const comp::Scale& s)
				{
					FLOAT3 translation = &p ? FLOAT3{ p.x, p.y, p.z } : FLOAT3{ 0.0f, 0.0f, 0.0f };
					FLOAT3 rotation = &r ? FLOAT3{ r.x, r.y, r.z } : FLOAT3{ 0.0f, 0.0f, 0.0f };
					FLOAT3 scale = &s ? FLOAT3{ s.x, s.y, s.z } : FLOAT3{ 1.0f, 1.0f, 1.0f };

					t.LocalToWorld = DirectX::XMMatrixScaling(scale.x, scale.y, scale.z) *
						DirectX::XMMatrixRotationRollPitchYaw(rotation.x, rotation.y, rotation.z) *
						DirectX::XMMatrixTranslation(translation.x, translation.y, translation.z);
				});

		m_ECSWorld.system < comp::Position, comp::RigidBody >("Physics")
			.kind(phasePhysics)
			.each([](comp::Position& p, comp::RigidBody& rb)
				{
					const float dt = 1.0f / 60.0f;
					FLOAT3& v = rb.Velocity;
					FLOAT3& f = rb.Force;
					v += f * dt;
					p.x += v.x * dt;
				});

		m_ECSWorld.system<>()
			.kind(phaseBeginRender)
			.each([this]()
				{
					float dt = m_ECSWorld.delta_time();
					m_pRenderer->Update(dt);	// TODO: 빼도 되나?
					m_pRenderer->BeginRender();
				});

		m_ECSWorld.system<const comp::Transform, const comp::MeshRenderer>("Render Mesh")
			.kind(phaseRender)
			.multi_threaded(false)	// TODO: test multi threading
			.each([this](const comp::Transform& t, const comp::MeshRenderer& mesh)
				{
					if (mesh.Mesh)
					{
						m_pRenderer->RenderMeshObject(mesh.Mesh, &t.LocalToWorld);
					}
				});

		m_ECSWorld.system<const comp::Transform, const comp::ProceduralSphereRenderer>("Render Procedural")
			.kind(phaseRender)
			.multi_threaded(false)	// TODO: test multi threading
			.each([this](const comp::Transform& t, const comp::ProceduralSphereRenderer& proc)
				{
					if (proc.Sphere)
					{
						m_pRenderer->RenderProceduralSphereObject(proc.Sphere, &t.LocalToWorld);
					}
				});

		m_ECSWorld.system<const comp::Transform, const comp::CasperRenderer>("Render CASPER")
			.kind(phaseRender)
			.multi_threaded(false)	// TODO: test multi threading
			.each([this](const comp::Transform& t, const comp::CasperRenderer& casper)
				{
					if (casper.Casper)
					{
						m_pRenderer->RenderCasperObject(casper.Casper, &t.LocalToWorld);
					}
				});

		m_ECSWorld.system<const comp::Position, const comp::Rotation, const comp::Scale, const comp::SpriteRenderer>("Render Sprite")
			.kind(phaseRender)
			.multi_threaded(false)	// TODO: test multi threading
			.each([this](const comp::Position& p, const comp::Rotation& r, const comp::Scale& s, const comp::SpriteRenderer& sprite)
				{
					ASSERT(sprite.Sprite, "SpriteRenderer. Sprite is null");
					if (sprite.Sprite)
					{
						m_pRenderer->RenderSprite(sprite.Sprite, (int)p.x, (int)p.y, s.x, s.y, 0.0f);
					}
				});

		m_ECSWorld.system<const comp::Position, const comp::Rotation, const comp::Scale, comp::TextRenderer>("Render Text")
			.kind(phaseRender)
			.multi_threaded(false)	// TODO: test multi threading
			.each([this](const comp::Position& p, const comp::Rotation& r, const comp::Scale& s, comp::TextRenderer& text)
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
		m_ECSWorld.observer<comp::MeshRenderer>()
			.event(flecs::OnRemove)
			.each([this](comp::MeshRenderer& m)
				{
					SAFE_RELEASE(m.Mesh);
				});

		m_ECSWorld.observer<comp::SpriteRenderer>()
			.event(flecs::OnRemove)
			.each([this](comp::SpriteRenderer& s)
				{
					SAFE_RELEASE(s.Sprite);
				});

		m_ECSWorld.observer<comp::TextRenderer>()
			.event(flecs::OnRemove)
			.each([this](comp::TextRenderer& t)
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
		auto randPosition = [](FLOAT2 range) -> comp::Position
			{
				ASSERT(range.y > range.x, "Invalid position range");
				comp::Position pos = {};
				pos.x = (float)((rand() % (int)(range.y - range.x + 1)) + range.x);
				pos.y = (float)((rand() % 7) - 3);
				pos.z = (float)((rand() % (int)(range.y - range.x + 1)) + range.x);
				return pos;
			};
		auto randRotation = [](FLOAT2 range) -> comp::Rotation
			{
				ASSERT(range.y > range.x, "Invalid rotation range");
				comp::Rotation rot = {};
				rot.x = DegToRad(static_cast<float>(rand() % (int)(range.y - range.x + 1) + range.x));
				rot.y = DegToRad(static_cast<float>(rand() % (int)(range.y - range.x + 1) + range.x));
				rot.z = DegToRad(static_cast<float>(rand() % (int)(range.y - range.x + 1) + range.x));
				return rot;
			};
		auto randScale = [](FLOAT2 range) -> comp::Scale
			{
				ASSERT(range.x > 0.01f && range.y > range.x, "Invalid scale range");
				comp::Scale scale = {};
				scale.x = (float)((rand() % (int)((range.y - range.x) * 10.0f + 1)) / 10.0f + range.x);
				scale.y = scale.x;
				scale.z = scale.x;
				return scale;
			};
		auto randRigidBody = []() -> comp::RigidBody
			{
				comp::RigidBody rb = {};
				rb.Velocity.x = (float)((rand() % 3) - 1);
				rb.Velocity.y = (float)((rand() % 3) - 1);
				rb.Velocity.z = (float)((rand() % 3) - 1);
				return rb;
			};

		// Create grid
		{
			flecs::entity e = m_ECSWorld.entity()
				.set<comp::Position>({ 0.0f, -5.0f, 0.0f })
				.set<comp::Rotation>({ 0.0f, 0.0f, 0.0f })
				.set<comp::Scale>({ 100.0f, 100.0f, 100.0f })
				.set<comp::Transform>({})
				.set<comp::MeshRenderer>({ createMetalTileGridMeshObject(m_pRenderer) });
			m_Entities.emplace_back(e.id());
		}
		//// wire fence
		//{
		//	flecs::entity e = m_ECSWorld.entity()
		//		.set<comp::Position>({ 10.0f, 0.0f, 25.0f })
		//		.set<comp::Rotation>({ PI / 2.0f, 0.0f, 0.0f })
		//		.set<comp::Scale>({ 10.0f, 10.0f, 10.0f })
		//		.set<comp::Transform>({})
		//		.set<comp::MeshRenderer>({ createWireWall(m_pRenderer) });
		//	m_Entities.emplace_back(e.id());
		//}
		//// Create kitty box entities
		//const uint KITTY_BOX_OBJECT_COUNT = 10;
		//for (uint i = 0; i < KITTY_BOX_OBJECT_COUNT; i++)
		//{
		//	flecs::entity e = m_ECSWorld.entity()
		//		.set<comp::Position>(randPosition({ -25, 25 }))
		//		.set<comp::Rotation>(randRotation({ -60, 60 }))
		//		.set<comp::Scale>(randScale({ 1,3 }))
		//		.set<comp::Transform>({})
		//		.set<comp::MeshRenderer>({ createKittyBoxMeshObject(m_pRenderer) });
		//	m_Entities.emplace_back(e.id());
		//}
		//// Create megayuchi box entities
		//const uint MEGA_BOX_OBJECT_COUNT = 5;
		//for (uint i = 0; i < MEGA_BOX_OBJECT_COUNT; i++)
		//{
		//	flecs::entity e = m_ECSWorld.entity()
		//		.set<comp::Position>(randPosition({ -25, 25 }))
		//		.set<comp::Rotation>(randRotation({ -60, 60 }))
		//		.set<comp::Scale>(randScale({ 1,3 }))
		//		.set<comp::Transform>({})
		//		.set<comp::MeshRenderer>({ createMegayuchiBoxMeshObject(m_pRenderer) });
		//	m_Entities.emplace_back(e.id());
		//}
		//// Create sphere entities
		//const uint SPHERE_OBJECT_COUNT = 1;
		//for (uint i = 0; i < SPHERE_OBJECT_COUNT; i++)
		//{
		//	flecs::entity e = m_ECSWorld.entity()
		//		.set<comp::Position>(randPosition({ -15, 15 }))
		//		.set<comp::Rotation>(randRotation({ -60, 60 }))
		//		.set<comp::Scale>({ 1.0f, 1.0f, 1.0f })
		//		.set<comp::Transform>({})
		//		.set<comp::RigidBody>(randRigidBody())
		//		.set<comp::MeshRenderer>({ createKannaSphereMeshObject(m_pRenderer) });
		//	m_Entities.emplace_back(e.id());
		//}

		//// Create sprite entity
		//{
		//	flecs::entity e = m_ECSWorld.entity()
		//		.set<comp::Position>({ 100.0f, 100.0f, 0.0f })
		//		.set<comp::Rotation>({ 0.0f, 0.0f, 0.0f })
		//		.set<comp::Scale>({ 0.1f, 0.1f, 0.1f })
		//		.set<comp::SpriteRenderer>({ L"./Resources/Kanna.dds" });

		//	m_Entities.emplace_back(e.id());
		//}
		//// Create text entity
		//{
		//	flecs::entity e = m_ECSWorld.entity()
		//		.set<comp::Position>({ 500.0f, 100.0f, 0.0f })
		//		.set<comp::Rotation>({ 0.0f, 0.0f, 0.0f })
		//		.set<comp::Scale>({ 1.0f, 1.0f, 1.0f })
		//		.set<comp::TextRenderer>(comp::TextRenderer(L"Hello"));

		//	m_Entities.emplace_back(e.id());
		//}

		if (m_pRenderer->IsRayTracingEnabled())
		{
			// Create CASPER entity
			{
				flecs::entity e = m_ECSWorld.entity()
					.set<comp::Position>({ -3.5f, -2.0f, 12.0f })
					.set<comp::Rotation>({ 0.0f, DegToRad(180.0f), 0.0f })
					.set<comp::Scale>({ 0.05f, 0.05f, 0.05f })
					.set<comp::Transform>({})
					.set<comp::MeshRenderer>({ createMeshFromFile(m_pRenderer, "./Resources/stanford_bunny_pbr.glb") });
				m_Entities.emplace_back(e.id());
			}
			{
				flecs::entity e = m_ECSWorld.entity()
					.set<comp::Position>({ 3.5f, -2.0f, 12.0f })
					.set<comp::Rotation>({ 0.0f, DegToRad(180.0f), 0.0f })
					.set<comp::Scale>({ 1.0f, 1.0f, 1.0f })
					.set<comp::Transform>({})
					.set<comp::CasperRenderer>({ createCasperObject(m_pRenderer, L"./Resources/temp/bunny") });
				m_Entities.emplace_back(e.id());
			}
			const uint BUNNY_CASPER_OBJECT_COUNT = 0;
			for (uint i = 0; i < BUNNY_CASPER_OBJECT_COUNT; i++)
			{
				flecs::entity e = m_ECSWorld.entity()
					.set<comp::Position>(randPosition({ -35, 35 }))
					.set<comp::Rotation>(randRotation({ -60, 60 }))
					.set<comp::Scale>({ 2.0f, 2.0f, 2.0f })
					.set<comp::Transform>({})
					.set<comp::RigidBody>(randRigidBody())
					.set<comp::Transform>({})
					.set<comp::CasperRenderer>({ createCasperObject(m_pRenderer, L"./Resources/temp/bunny") });
				m_Entities.emplace_back(e.id());
			}
			// Create volumes
			{
				//Volume vol;
				//bool bLoaded = vol.load("./Resources/cubq/glycon.dag");
				//ASSERT(bLoaded, "Failed to load volume data");

				//Image diffuseProjImages[6];
				//Image depthProjImages[6];
				//for (int i = 0; i < 6; i++)
				//{
				//	ProjectVolumeFace(vol, i, 4096, &diffuseProjImages[i], &depthProjImages[i]);
				//}
				//IBounds bounds = vol.getOccupiedBounds();
				//{
				//	std::filesystem::path metaPath = "./Resources/temp/glycon/metadata.txt";
				//	std::ofstream ofs(metaPath);
				//	if (!ofs)
				//	{
				//		std::cerr << "[SaveAsCasper] Failed to open metadata: " << metaPath << "\n";
				//		return false;
				//	}

				//	ofs << "Min" << " " << bounds.Min.x << " " << bounds.Min.y << " " << bounds.Min.z << "\n";
				//	ofs << "Max" << " " << bounds.Max.x << " " << bounds.Max.y << " " << bounds.Max.z << "\n";
				//}
				//Image::SaveToCubeMapDDS(L"./Resources/temp/glycon/casper_color.dds", diffuseProjImages, /*bGenerateMips*/true, /*mipLevels*/0);
				//Image::SaveToCubeMapDDS(L"./Resources/temp/glycon/casper_depth.dds", depthProjImages, /*bGenerateMips*/false, /*mipLevels*/0);

				flecs::entity e = m_ECSWorld.entity()
					.set<comp::Position>({ 5.0f, 0.0f, 3.0f })
					.set<comp::Rotation>({ DegToRad(-90.0f), 0.0f, 0.0f })
					.set<comp::Scale>({ 0.005f, 0.005f, 0.005f })
					.set<comp::Transform>({})
					.set<comp::CasperRenderer>({ createCasperObject(m_pRenderer, L"./Resources/temp/glycon") });
				m_Entities.emplace_back(e.id());
			}
			{
				//Volume vol;
				//bool bLoaded = vol.load("./Resources/cubq/building.dag");
				//ASSERT(bLoaded, "Failed to load volume data");
			
				//Image diffuseProjImages[6];
				//Image depthProjImages[6];
				//for (int i = 0; i < 6; i++)
				//{
				//	ProjectVolumeFace(vol, i, 10000, &diffuseProjImages[i], &depthProjImages[i]);
				//}
				//IBounds bounds = vol.getOccupiedBounds();
				//{
				//	std::filesystem::path metaPath = "./Resources/temp/building/metadata.txt";
				//	std::ofstream ofs(metaPath);
				//	if (!ofs)
				//	{
				//		std::cerr << "[SaveAsCasper] Failed to open metadata: " << metaPath << "\n";
				//		return false;
				//	}

				//	ofs << "Min" << " " << bounds.Min.x << " " << bounds.Min.y << " " << bounds.Min.z << "\n";
				//	ofs << "Max" << " " << bounds.Max.x << " " << bounds.Max.y << " " << bounds.Max.z << "\n";
				//}

				//for (int i = 0; i < 6; i++)
				//{
				//	depthProjImages[i].Save(L"./Resources/temp/building/depth_face_" + std::to_wstring(i) + L".dds", Image::EImageFormat::DDS);
				//}

				//Image::SaveToCubeMapDDS(L"./Resources/temp/building/casper_color.dds", diffuseProjImages, /*bGenerateMips*/true, /*mipLevels*/0);
				//Image::SaveToCubeMapDDS(L"./Resources/temp/building/casper_depth.dds", depthProjImages, /*bGenerateMips*/false, /*mipLevels*/0);

				flecs::entity e = m_ECSWorld.entity()
					.set<comp::Position>({ -25.0f, 0.0f, 12.0f })
					.set<comp::Rotation>({ DegToRad(-90.0f), DegToRad(180.0f), 0.0f })
					.set<comp::Scale>({ 0.01f, 0.01f, 0.01f })
					.set<comp::Transform>({})
					.set<comp::CasperRenderer>({ createCasperObject(m_pRenderer, L"./Resources/temp/building") });
				m_Entities.emplace_back(e.id());
			}
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
	m_PrevUpdateTick = currTick;
	if (elapsedMs > 1000) return true;
	const float dt = (float)elapsedMs * 0.001f; // ms -> h

	const float SPEED = 100.0f;
	if (m_CamOffsetX || m_CamOffsetY || m_CamOffsetZ)
	{
		m_pRenderer->MoveCamera(
			m_CamOffsetX * SPEED * dt,
			m_CamOffsetY * SPEED * dt,
			m_CamOffsetZ * SPEED * dt);
	}

	m_ECSWorld.progress(dt);

	m_TimeOfDay += dt * 10.0f / 3600.0f;
	m_TimeOfDay = fmodf(m_TimeOfDay, 24.0f); // 0 - 24 hours

	FLOAT3 sunDir = ComputeSunDir(m_TimeOfDay, /*latitude*/37.6f, /*declination*/15.0f);

	m_pRenderer->SetSunDir(-sunDir);
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