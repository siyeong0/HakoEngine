#include "pch.h"
#include "D3D12ResourceManager.h"
#include "SimpleConstantBufferPool.h"
#include "SingleDescriptorAllocator.h"
#include "ShaderManager.h"
#include "PSOManager.h"
#include "RootSignatureManager.h"
#include "DescriptorPool.h"
#include "RayTracingManager.h"
#include "D3D12Renderer.h"
#include "BasicMeshObject.h"

STDMETHODIMP BasicMeshObject::QueryInterface(REFIID refiid, void** ppv)
{
	return E_NOINTERFACE;
}

STDMETHODIMP_(ULONG) BasicMeshObject::AddRef()
{
	m_RefCount++;
	return m_RefCount;
}

STDMETHODIMP_(ULONG) BasicMeshObject::Release()
{
	int refCount = --m_RefCount;
	if (!m_RefCount)
	{
		delete this;
	}
	return refCount;
}

bool ENGINECALL BasicMeshObject::BeginCreateMesh(const Vertex* vertices, uint numVertices, uint numTriGroups)
{
	ID3D12Device5* pD3DDeivce = m_pRenderer->GetD3DDevice();
	D3D12ResourceManager* pResourceManager = m_pRenderer->GetResourceManager();
	BOOL bUseGpuUploadHepas = m_pRenderer->IsGpuUploadHeapsEnabledInl();

	ASSERT(numTriGroups <= MAX_TRI_GROUP_COUNT_PER_OBJ, "Too many tri-groups.");

	if (FAILED(pResourceManager->CreateVertexBuffer(sizeof(Vertex), numVertices, &m_VertexBufferView, &m_pVertexBuffer, (void*)vertices, bUseGpuUploadHepas)))
	{
		ASSERT(false, "Failed to create vertex buffer.");
		return false;
	}
	
	m_MaxNumTriGroups = numTriGroups;
	m_pTriGroupList = new IndexedTriGroup[m_MaxNumTriGroups];
	memset(m_pTriGroupList, 0, sizeof(IndexedTriGroup) * m_MaxNumTriGroups);

	return true;
}

bool ENGINECALL BasicMeshObject::InsertTriGroup(
	const uint16_t* indices, uint numTriangles, 
	const wchar_t* diffuseFilePathOrNull, 
	const wchar_t* normalFilePathOrNull)
{
	ID3D12Device5* pD3DDeivce = m_pRenderer->GetD3DDevice();
	size_t srvDescriptorSize = m_pRenderer->GetSrvDescriptorSize();
	D3D12ResourceManager* pResourceManager = m_pRenderer->GetResourceManager();
	SingleDescriptorAllocator* pSingleDescriptorAllocator = m_pRenderer->GetSingleDescriptorAllocator();
	bool bUseGpuUploadHeaps = m_pRenderer->IsGpuUploadHeapsEnabledInl();

	ID3D12Resource* pIndexBuffer = nullptr;
	D3D12_INDEX_BUFFER_VIEW indexBufferView = {};

	ASSERT(m_NumTriGroups < m_MaxNumTriGroups, "Too many tri-groups.");

	if (FAILED(pResourceManager->CreateIndexBuffer(numTriangles * 3, &indexBufferView, &pIndexBuffer, (void*)indices, bUseGpuUploadHeaps)))
	{
		ASSERT(false, "Failed to create index buffer.");
		return false;
	}
	IndexedTriGroup* pTriGroup = m_pTriGroupList + m_NumTriGroups;
	pTriGroup->IndexBuffer = pIndexBuffer;
	pTriGroup->IndexBufferView = indexBufferView;
	pTriGroup->NumTriangles = static_cast<uint>(numTriangles);
	pTriGroup->DiffuseTexHandle = diffuseFilePathOrNull 
		? (TextureHandle*)m_pRenderer->CreateTextureFromFile(diffuseFilePathOrNull) 
		: (TextureHandle*)m_pRenderer->CreateImmutableTexture(CreateSolidColorImageRGBA(128, 128, RGBA{255,255,255,255}));
	pTriGroup->NormalTexHandle = normalFilePathOrNull
		? (TextureHandle*)m_pRenderer->CreateTextureFromFile(normalFilePathOrNull)
		: (TextureHandle*)m_pRenderer->CreateImmutableTexture(CreateSolidColorImageRGBA(128, 128, RGBA{ 128,128,255,255 }));
	pTriGroup->Material = CreateBasicMaterial(MATERIAL_TYPE_DEFAULT);
	m_NumTriGroups++;

	return true;
}

bool ENGINECALL BasicMeshObject::InsertTriGroup(const uint16_t* indices, uint numTriangles, const Material& material)
{
	ID3D12Device5* pD3DDeivce = m_pRenderer->GetD3DDevice();
	size_t srvDescriptorSize = m_pRenderer->GetSrvDescriptorSize();
	D3D12ResourceManager* pResourceManager = m_pRenderer->GetResourceManager();
	SingleDescriptorAllocator* pSingleDescriptorAllocator = m_pRenderer->GetSingleDescriptorAllocator();
	bool bUseGpuUploadHeaps = m_pRenderer->IsGpuUploadHeapsEnabledInl();

	ID3D12Resource* pIndexBuffer = nullptr;
	D3D12_INDEX_BUFFER_VIEW indexBufferView = {};

	ASSERT(m_NumTriGroups < m_MaxNumTriGroups, "Too many tri-groups.");

	if (FAILED(pResourceManager->CreateIndexBuffer(numTriangles * 3, &indexBufferView, &pIndexBuffer, (void*)indices, bUseGpuUploadHeaps)))
	{
		ASSERT(false, "Failed to create index buffer.");
		return false;
	}
	IndexedTriGroup* pTriGroup = m_pTriGroupList + m_NumTriGroups;
	pTriGroup->IndexBuffer = pIndexBuffer;
	pTriGroup->IndexBufferView = indexBufferView;
	pTriGroup->NumTriangles = static_cast<uint>(numTriangles);
	pTriGroup->DiffuseTexHandle = material.Diffuse.IsValid()
		? (TextureHandle*)m_pRenderer->CreateImmutableTexture(material.Diffuse)
		: (TextureHandle*)m_pRenderer->CreateImmutableTexture(CreateSolidColorImageRGBA(128, 128, RGBA{ 255,255,255,255 }));
	pTriGroup->NormalTexHandle = material.Normal.IsValid()
		? (TextureHandle*)m_pRenderer->CreateImmutableTexture(material.Normal)
		: (TextureHandle*)m_pRenderer->CreateImmutableTexture(CreateSolidColorImageRGBA(128, 128, RGBA{ 128,128,255,255 }));
	pTriGroup->Material = CreateBasicMaterial(MATERIAL_TYPE_DEFAULT);
	pTriGroup->Material = CreateBasicMaterial(material.Type);
	m_NumTriGroups++;
	return true;
}

void ENGINECALL BasicMeshObject::EndCreateMesh(bool bOpaque, bool bUseRayTracingIfSupported)
{
	bool bUseRayTracing = bUseRayTracingIfSupported && m_pRenderer->IsRayTracingEnabledInl();

	if (bOpaque && !bUseRayTracing)
	{
		m_RenderPass = RENDER_PASS_OPAQUE;
	}
	else if (bOpaque && bUseRayTracing)
	{
		m_RenderPass = RENDER_PASS_RAYTRACING_OPAQUE;
	}
	else if (!bOpaque && !bUseRayTracing)
	{
		m_RenderPass = RENDER_PASS_TRANSPARENT;
	}
	else if (!bOpaque && bUseRayTracing)
	{
		m_RenderPass = RENDER_PASS_RAYTRACING_TRANSPARENT;
	}

	if (bUseRayTracing)
	{
		RayTracingManager* pRayTracingManager = m_pRenderer->GetRayTracingManager();
		m_pBLASHandle = pRayTracingManager->AllocBLAS(m_pVertexBuffer, sizeof(Vertex), m_VertexBufferView.SizeInBytes / sizeof(Vertex), m_pTriGroupList, m_NumTriGroups, false);
	}
}

uint ENGINECALL BasicMeshObject::GetRenderPass()
{
	return m_RenderPass;
}

bool BasicMeshObject::Initialize(D3D12Renderer* pRenderer)
{
	bool bResult = false;
	m_pRenderer = pRenderer;

	initPipelineState();

	return bResult;
}

void BasicMeshObject::Draw(int threadIndex, ID3D12GraphicsCommandList6* pCommandList, const Matrix4x4* worldMatrix)
{
	ID3D12Device5* pDevice = m_pRenderer->GetD3DDevice();
	uint srvDescriptorSize = m_pRenderer->GetSrvDescriptorSize();
	DescriptorPool* pDescriptorPool = m_pRenderer->GetDescriptorPool(threadIndex);
	SimpleConstantBufferPool* pMeshConstantBufferPool = m_pRenderer->GetConstantBufferPool(CONSTANT_BUFFER_TYPE_MESH, threadIndex);

	// --- 1) Constant buffer alloc and intialization. (as root cbv)
	ConstantBufferContainer* cb = pMeshConstantBufferPool->Alloc();
	ASSERT(cb, "Failed to allocate constant buffer.");
	CONSTANT_BUFFER_MESH_OBJECT* pCBPerDraw = (CONSTANT_BUFFER_MESH_OBJECT*)cb->pSystemMemAddr;
	pCBPerDraw->WorldMatrix = XMMatrixTranspose(*worldMatrix);

	// --- 2) SRV Descriptor table (TriGroup 개수 만큼)
	static constexpr uint NUM_SRV_PER_TRIGROUP = 2;
	const uint requiredSrvCount = static_cast<uint>(m_NumTriGroups) * NUM_SRV_PER_TRIGROUP;

	CD3DX12_CPU_DESCRIPTOR_HANDLE cpuDescriptorTable = {};
	CD3DX12_GPU_DESCRIPTOR_HANDLE gpuDescriptorTable = {};
	bool bOk = pDescriptorPool->AllocDescriptorTable(&cpuDescriptorTable, &gpuDescriptorTable, requiredSrvCount);
	ASSERT(bOk, "Failed to allocate descriptor table.");

	CD3DX12_CPU_DESCRIPTOR_HANDLE cpuCurrDescHandleAddress = cpuDescriptorTable;
	for (uint i = 0; i < m_NumTriGroups; ++i)
	{
		const IndexedTriGroup& tg = m_pTriGroupList[i];
		TextureHandle* pDiffuseTex = tg.DiffuseTexHandle;
		TextureHandle* pNormalTex = tg.NormalTexHandle;
		ASSERT(pDiffuseTex && pDiffuseTex->SRV.ptr != 0, "Texture SRV missing.");

		pDevice->CopyDescriptorsSimple(1, cpuCurrDescHandleAddress, pDiffuseTex->SRV, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
		cpuCurrDescHandleAddress.Offset(1, srvDescriptorSize);

		pDevice->CopyDescriptorsSimple(1, cpuCurrDescHandleAddress, pNormalTex->SRV, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
		cpuCurrDescHandleAddress.Offset(1, srvDescriptorSize);
	}

	// --- 3) PSO/RS/DescHeap binding.
	pCommandList->SetPipelineState(m_pPSOHandle->pPSO);
	pCommandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	pCommandList->IASetVertexBuffers(0, 1, &m_VertexBufferView);

	// --- 4) TriGroup loop: t0가 가리키는 SRV를 매 드로우마다 바꿈
	CD3DX12_GPU_DESCRIPTOR_HANDLE gpuCurrDescHandleAddress = gpuDescriptorTable; // 첫 TriGroup의 t0
	for (uint i = 0; i < m_NumTriGroups; ++i)
	{
		pCBPerDraw->Material = m_pTriGroupList[i].Material;
		pCommandList->SetGraphicsRootConstantBufferView(ROOT_SLOT_CBV_PER_DRAW, cb->pGPUMemAddr);

		// i번째 TriGroup의 SRV 테이블 시작(t0=diffuse, t1=normal)
		pCommandList->SetGraphicsRootDescriptorTable(ROOT_SLOT_SRV_TABLE, gpuCurrDescHandleAddress);

		// 인덱스 버퍼/드로우
		const IndexedTriGroup& tg = m_pTriGroupList[i];
		pCommandList->IASetIndexBuffer(&tg.IndexBufferView);
		pCommandList->DrawIndexedInstanced(tg.NumTriangles * 3, 1, 0, 0, 0);

		// 다음 TriGroup의 SRV 시작점으로 이동
		gpuCurrDescHandleAddress.Offset(NUM_SRV_PER_TRIGROUP, srvDescriptorSize);
	}
}

void BasicMeshObject::UpdateBLASTransform(const Matrix4x4& worldMatrix)
{
	RayTracingManager* pRayTracingManager = m_pRenderer->GetRayTracingManager();
	pRayTracingManager->UpdateBLASTransform(m_pBLASHandle, worldMatrix);
}

RenderMaterial BasicMeshObject::CreateBasicMaterial(MATERIAL_TYPE mtlType)
{
	RenderMaterial out;
	out.Type = mtlType;
	out.Ks = FLOAT3(0.9f, 0.9f, 0.9f);
	out.Roughness = 0.01f;
	out.Kr = FLOAT3(0.5f, 0.5f, 0.5f);
	out.Kt = FLOAT3(0.0f, 0.0f, 0.0f);
	out.Type = MATERIAL_TYPE_DEFAULT;
	out.AmbientIntensity = 0.25f;
	out.Opacity = FLOAT3(1.0f, 1.0f, 1.0f);

	if (mtlType == MATERIAL_TYPE_GLASS)
	{
		out.Ks = FLOAT3(0.1f, 0.1f, 0.1f);
		out.Kr = FLOAT3(0.05f, 0.05f, 0.05f);
		out.Kt = FLOAT3(0.95f, 0.95f, 0.95f);
		out.Opacity = FLOAT3(0.5f, 0.5f, 0.5f);
		out.AmbientIntensity = 0.01f;
	}

	if (mtlType == MATERIAL_TYPE_MATTE)
	{
		out.Kr = FLOAT3(0.0f, 0.0f, 0.0f);
	}

	return out;
}

bool BasicMeshObject::initPipelineState()
{
	HRESULT hr = S_OK;

	ID3D12Device5* pD3DDeivce = m_pRenderer->GetD3DDevice();
	ShaderManager* pShaderManager = m_pRenderer->GetShaderManager();
	RootSignatureManager* pRootSignatureManager = m_pRenderer->GetRootSignatureManager();
	PSOManager* pPsoManager = m_pRenderer->GetPSOManager();

	ShaderHandle* pVertexShader = pShaderManager->CreateShaderDXC(L"Standard.hlsl", L"VSMain", L"vs_6_0", 0);
	ASSERT(pVertexShader, "Shader compilation failed.");

	ShaderHandle* pPixelShader = pShaderManager->CreateShaderDXC(L"Standard.hlsl", L"PSMain", L"ps_6_0", 0);
	ASSERT(pPixelShader, "Shader compilation failed.");

	// Define the vertex input layout.
	D3D12_INPUT_ELEMENT_DESC inputElementDescs[] =
	{
		{ "POSITION",	0, DXGI_FORMAT_R32G32B32_FLOAT,	0, 0,	D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
		{ "TEXCOORD",	0, DXGI_FORMAT_R32G32_FLOAT,	0, 12,	D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
		{ "NORMAL",		0, DXGI_FORMAT_R32G32B32_FLOAT,	0, 20,	D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
		{ "TANGENT",	0, DXGI_FORMAT_R32G32B32_FLOAT,	0, 32,	D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
	};
	static_assert(sizeof(Vertex) == 44, "BasicVertex was changed. Please update the input layout.");

	// Describe and create the graphics pipeline state object (PSO).
	D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = {};
	psoDesc.InputLayout = { inputElementDescs, _countof(inputElementDescs) };
	psoDesc.pRootSignature = pRootSignatureManager->Query(ERootSignatureType::GraphicsDefault);
	psoDesc.VS = CD3DX12_SHADER_BYTECODE(pVertexShader->CodeBuffer, pVertexShader->CodeSize);
	psoDesc.PS = CD3DX12_SHADER_BYTECODE(pPixelShader->CodeBuffer, pPixelShader->CodeSize);
	psoDesc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
	psoDesc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
	psoDesc.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
	psoDesc.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;
	psoDesc.DepthStencilState.StencilEnable = FALSE;
	//psoDesc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
	psoDesc.RasterizerState.CullMode = D3D12_CULL_MODE_BACK;
	psoDesc.SampleMask = UINT_MAX;
	psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
	psoDesc.NumRenderTargets = 1;
	psoDesc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
	psoDesc.DSVFormat = DXGI_FORMAT_D32_FLOAT;
	psoDesc.SampleDesc.Count = 1;

	m_pPSOHandle = pPsoManager->CreatePSO(psoDesc, "BasicMesh");
	ASSERT(m_pPSOHandle, "Failed to query pipeline state.");

	if (pVertexShader)
	{
		pShaderManager->ReleaseShader(pVertexShader);
		pVertexShader = nullptr;
	}
	if (pPixelShader)
	{
		pShaderManager->ReleaseShader(pPixelShader);
		pPixelShader = nullptr;
	}
	return true;
}

void BasicMeshObject::deleteTriGroup(IndexedTriGroup* pTriGroup)
{

}

void BasicMeshObject::cleanup()
{
	m_pRenderer->EnsureCompleted();

	// delete all triangles-group
	if (m_pTriGroupList)
	{
		for (uint i = 0; i < m_NumTriGroups; i++)
		{
			SAFE_RELEASE(m_pTriGroupList[i].IndexBuffer);
			SAFE_CLEANUP(m_pTriGroupList[i].DiffuseTexHandle, m_pRenderer->DeleteTexture);
		}
		SAFE_DELETE_ARRAY(m_pTriGroupList);
	}

	SAFE_RELEASE(m_pVertexBuffer);
	SAFE_CLEANUP(m_pPSOHandle, m_pRenderer->GetPSOManager()->ReleasePSO);
	SAFE_CLEANUP(m_pBLASHandle, m_pRenderer->GetRayTracingManager()->FreeBLAS);
}