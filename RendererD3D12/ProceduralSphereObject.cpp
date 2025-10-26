#include "pch.h"
#include "Interface/IProceduralSphereObject.h"
#include "D3D12Renderer.h"
#include "RayTracingManager.h"
#include "D3D12ResourceManager.h"
#include "ProceduralSphereObject.h"


STDMETHODIMP ProceduralSphereObject::QueryInterface(REFIID refiid, void** ppv)
{
	return E_NOINTERFACE;
}

STDMETHODIMP_(ULONG) ProceduralSphereObject::AddRef()
{
	m_RefCount++;
	return m_RefCount;
}

STDMETHODIMP_(ULONG) ProceduralSphereObject::Release()
{
	int refCount = --m_RefCount;
	if (!m_RefCount)
	{
		delete this;
	}
	return refCount;
}

bool ENGINECALL ProceduralSphereObject::BeginCreateGeom(uint numSpheres, const Material& material)
{
	m_MaxNumSpheres = numSpheres;
	m_SphereDataArray.reserve(m_MaxNumSpheres);

	m_Material.BaseColor = material.BaseColor;
	m_Material.Opacity = material.Opacity;
	m_Material.SpecularColor = material.SpecularColor;
	m_Material.SpecularFactor = material.SpecularFactor;
	m_Material.MetallicFactor = material.MetallicFactor;
	m_Material.RoughnessFactor = material.RoughnessFactor;
	m_Material.NormalScale = material.NormalScale;
	m_Material.AmbientOcclusionStrength = material.AmbientOcclusionStrength;

	m_bOpaque = material.IsOpaque();

	return true;
}

bool ENGINECALL ProceduralSphereObject::InsertSphere(const Sphere& sphereData)
{
	m_SphereDataArray.emplace_back(sphereData);
	ASSERT(m_SphereDataArray.size() <= m_MaxNumSpheres, "Exceeded maximum number of spheres.");
	return true;
}

void ENGINECALL ProceduralSphereObject::EndCreateGeom(bool bUseRayTracingIfSupported)
{
	bool bUseRayTracing = bUseRayTracingIfSupported && m_pRenderer->IsRayTracingEnabledInl();

	if (bUseRayTracing)
	{
		m_RenderPass = RENDER_PASS_RAYTRACING;
	}
	else
	{
		// Procedural sphere supports only raytracing rendering path.
	}


	if (bUseRayTracing)
	{
		std::vector<D3D12_RAYTRACING_AABB> aabbs(m_SphereDataArray.size());
		for (uint i = 0; i < m_SphereDataArray.size(); ++i)
		{
			const Sphere& s = m_SphereDataArray[i];
			const FLOAT3 c = { s.Center.x, s.Center.y, s.Center.z };
			const float  r = s.Radius;
			aabbs[i].MinX = c.x - r; aabbs[i].MinY = c.y - r; aabbs[i].MinZ = c.z - r;
			aabbs[i].MaxX = c.x + r; aabbs[i].MaxY = c.y + r; aabbs[i].MaxZ = c.z + r;
		}

		m_pRenderer->GetResourceManager()->CreateStructuredBuffer(
			sizeof(D3D12_RAYTRACING_AABB),
			aabbs.size(),
			&m_pAABBBuffer,
			aabbs.data(),
			m_pRenderer->IsGpuUploadHeapsEnabled());

		m_pRenderer->GetResourceManager()->CreateStructuredBuffer(
			sizeof(Sphere),
			m_SphereDataArray.size(),
			&m_pSphereDataBuffer,
			m_SphereDataArray.data(),
			m_pRenderer->IsGpuUploadHeapsEnabled());

		RayTracingManager* pRayTracingManager = m_pRenderer->GetRayTracingManager();
		m_pBLASHandle = pRayTracingManager->AllocBLASSpheres(
			m_pAABBBuffer,
			static_cast<uint>(aabbs.size()),
			sizeof(D3D12_RAYTRACING_AABB),
			m_pSphereDataBuffer,
			static_cast<uint>(m_SphereDataArray.size()),
			sizeof(Sphere),
			m_Material,
			m_bOpaque,
			false);
	}

	m_SphereDataArray.clear();
}

uint ENGINECALL ProceduralSphereObject::GetRenderPass()
{
	return m_RenderPass;
}

bool ProceduralSphereObject::Initialize(D3D12Renderer* pRenderer)
{
	bool bResult = false;
	m_pRenderer = pRenderer;

	return bResult;
}

void ProceduralSphereObject::Draw(int threadIndex, ID3D12GraphicsCommandList6* pCommandList, const Matrix4x4& worldMatrix)
{
	// Nothing to draw directly; handled via raytracing.
}

void ProceduralSphereObject::UpdateBLAS(int threadIndex, ID3D12GraphicsCommandList6* pCommandList, const Matrix4x4& worldMatrix)
{
	RayTracingManager* pRayTracingManager = m_pRenderer->GetRayTracingManager();
	pRayTracingManager->UpdateBLAS(threadIndex, pCommandList, m_pBLASHandle, worldMatrix);
}

void ProceduralSphereObject::cleanup()
{
	SAFE_RELEASE(m_pAABBBuffer);
	SAFE_RELEASE(m_pSphereDataBuffer);
	SAFE_CLEANUP(m_pBLASHandle, m_pRenderer->GetRayTracingManager()->FreeBLAS);
}