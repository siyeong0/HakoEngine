#include "pch.h"
#include "Interface/ICasperObject.h"
#include "RayTracingManager.h"
#include "D3D12ResourceManager.h"
#include "D3D12Renderer.h"
#include "CasperObject.h"

STDMETHODIMP CasperObject::QueryInterface(REFIID refiid, void** ppv)
{
	return E_NOINTERFACE;
}

STDMETHODIMP_(ULONG) CasperObject::AddRef()
{
	m_RefCount++;
	return m_RefCount;
}

STDMETHODIMP_(ULONG) CasperObject::Release()
{
	int refCount = --m_RefCount;
	if (!m_RefCount)
	{
		delete this;
	}
	return refCount;
}

bool ENGINECALL CasperObject::BeginCreateCasper(uint numAtlases, const UMaterial& material)
{
	m_MaxNumAtlases = numAtlases;
	m_Atlases.reserve(m_MaxNumAtlases);
	m_AtlasBoundsArray.reserve(m_MaxNumAtlases);

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

bool ENGINECALL CasperObject::InsertCasperAtlas(const CasperAtlas& atlas)
{
	
	TextureHandle* pDiffuse = (TextureHandle*)m_pRenderer->CreateTextureFromFile(atlas.DiffuseAtlas.c_str());
	TextureHandle* pDepth = (TextureHandle*)m_pRenderer->CreateTextureFromFile(atlas.DepthAtlas.c_str());
	m_Atlases.emplace_back(pDiffuse, pDepth);
	m_AtlasBoundsArray.emplace_back(atlas.AtlasBounds);

	ASSERT(m_Atlases.size() <= m_MaxNumAtlases, "Exceeded maximum number of casper atlases.");
	return true;
}

void ENGINECALL CasperObject::EndCreateCasper(bool bUseRayTracingIfSupported)
{
	bool bUseRayTracing = bUseRayTracingIfSupported && m_pRenderer->IsRayTracingEnabledInl();

	if (bUseRayTracing)
	{
		m_RenderPass = RENDER_PASS_RAYTRACING;
	}
	else
	{
		ASSERT(false, "CASPER supports only raytracing rendering path.");
	}


	if (bUseRayTracing)
	{
		std::vector<D3D12_RAYTRACING_AABB> aabbs(m_Atlases.size());
		for (uint i = 0; i < m_Atlases.size(); ++i)
		{
			const Bounds& bounds = m_AtlasBoundsArray[i];
			aabbs[i].MinX = bounds.Min.x; 
			aabbs[i].MinY = bounds.Min.y;
			aabbs[i].MinZ = bounds.Min.z;
			aabbs[i].MaxX = bounds.Max.x;
			aabbs[i].MaxY = bounds.Max.y;
			aabbs[i].MaxZ = bounds.Max.z;
		}

		m_pRenderer->GetResourceManager()->CreateStructuredBuffer(
			sizeof(D3D12_RAYTRACING_AABB),
			aabbs.size(),
			&m_pAABBBuffer,
			aabbs.data(),
			m_pRenderer->IsGpuUploadHeapsEnabled());

		RayTracingManager* pRayTracingManager = m_pRenderer->GetRayTracingManager();
		m_pBLASHandle = pRayTracingManager->AllocBLASCasper(
			m_pAABBBuffer,
			static_cast<uint>(aabbs.size()),
			sizeof(D3D12_RAYTRACING_AABB),
			m_Atlases,
			m_Material,
			m_bOpaque,
			false);
	}
}

uint ENGINECALL CasperObject::GetRenderPass()
{
	return m_RenderPass;
}

bool CasperObject::Initialize(D3D12Renderer* pRenderer)
{
	bool bResult = true;
	m_pRenderer = pRenderer;

	return bResult;
}

void CasperObject::Draw(int threadIndex, ID3D12GraphicsCommandList6* pCommandList, const Matrix4x4& worldMatrix)
{
	// Nothing to draw directly; handled via raytracing.
}

void CasperObject::UpdateBLAS(int threadIndex, ID3D12GraphicsCommandList6* pCommandList, const Matrix4x4& worldMatrix)
{
	RayTracingManager* pRayTracingManager = m_pRenderer->GetRayTracingManager();
	pRayTracingManager->UpdateBLAS(threadIndex, pCommandList, m_pBLASHandle, worldMatrix);
}

void CasperObject::cleanup()
{
	SAFE_RELEASE(m_pAABBBuffer);
	SAFE_CLEANUP(m_pBLASHandle, m_pRenderer->GetRayTracingManager()->FreeBLAS);
	for (auto& [a, b] : m_Atlases)
	{
		SAFE_CLEANUP(a, m_pRenderer->DeleteTexture);
		SAFE_CLEANUP(b, m_pRenderer->DeleteTexture);
	}
	m_Atlases.clear();
}