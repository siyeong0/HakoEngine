#include "pch.h"
#include "D3D12ResourceManager.h"
#include "SimpleConstantBufferPool.h"
#include "SingleDescriptorAllocator.h"
#include "ShaderManager.h"
#include "PSOManager.h"
#include "RootSignatureManager.h"
#include "DescriptorPool.h"
#include "D3D12Renderer.h"
#include "BasicMeshObject.h"

using namespace DirectX;

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

bool ENGINECALL BasicMeshObject::BeginCreateMesh(const Vertex* vertices, size_t numVertices, size_t numTriGroups)
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

bool ENGINECALL BasicMeshObject::InsertTriGroup(const uint16_t* indices, size_t numTriangles, const WCHAR* wchTexFileName)
{
	ID3D12Device5* pD3DDeivce = m_pRenderer->GetD3DDevice();
	size_t srvDescriptorSize = m_pRenderer->GetSrvDescriptorSize();
	D3D12ResourceManager* pResourceManager = m_pRenderer->GetResourceManager();
	SingleDescriptorAllocator* pSingleDescriptorAllocator = m_pRenderer->GetSingleDescriptorAllocator();
	BOOL bUseGpuUploadHeaps = m_pRenderer->IsGpuUploadHeapsEnabledInl();

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
	pTriGroup->NumTriangles = static_cast<UINT>(numTriangles);
	pTriGroup->pTexHandle = (TextureHandle*)m_pRenderer->CreateTextureFromFile(wchTexFileName);
	m_NumTriGroups++;
	return true;
}

void ENGINECALL BasicMeshObject::EndCreateMesh()
{

}

bool BasicMeshObject::Initialize(D3D12Renderer* pRenderer)
{
	bool bResult = false;
	m_pRenderer = pRenderer;

	initPipelineState();

	return bResult;
}

void BasicMeshObject::Draw(int threadIndex, ID3D12GraphicsCommandList6* pCommandList, const XMMATRIX* worldMatrix)
{
	ID3D12Device5* pDevice = m_pRenderer->GetD3DDevice();
	UINT srvDescriptorSize = m_pRenderer->GetSrvDescriptorSize();
	DescriptorPool* pDescriptorPool = m_pRenderer->GetDescriptorPool(threadIndex);
	SimpleConstantBufferPool* pMeshConstantBufferPool = m_pRenderer->GetConstantBufferPool(CONSTANT_BUFFER_TYPE_MESH, threadIndex);
	PSOManager* pPSOManager = m_pRenderer->GetPSOManager();

	// --- 1) Constant buffer alloc and intialization. (as root cbv)
	ConstantBufferContainer* cb = pMeshConstantBufferPool->Alloc();
	ASSERT(cb, "Failed to allocate constant buffer.");

	CB_MeshObject* pCBPerDraw = (CB_MeshObject*)cb->pSystemMemAddr;
	pCBPerDraw->WorldMatrix = XMMatrixTranspose(*worldMatrix);

	// --- 2) SRV Descriptor table (TriGroup 개수 만큼)
	CD3DX12_CPU_DESCRIPTOR_HANDLE cpuDescriptorTable = {};
	CD3DX12_GPU_DESCRIPTOR_HANDLE gpuDescriptorTable = {};
	const UINT requiredSrvCount = static_cast<UINT>(m_NumTriGroups);
	bool bOk = pDescriptorPool->AllocDescriptorTable(&cpuDescriptorTable, &gpuDescriptorTable, requiredSrvCount);
	ASSERT(bOk, "Failed to allocate descriptor table.");

	CD3DX12_CPU_DESCRIPTOR_HANDLE cpuCurrDescHandleAddress = cpuDescriptorTable;
	for (UINT i = 0; i < m_NumTriGroups; ++i)
	{
		const IndexedTriGroup& tg = m_pTriGroupList[i];
		TextureHandle* pTex = tg.pTexHandle;
		ASSERT(pTex && pTex->SRV.ptr != 0, "Texture SRV missing.");

		// SRV 하나씩 렌더링 힙에 복사 (t0로 쓸 자리)
		pDevice->CopyDescriptorsSimple(1, cpuCurrDescHandleAddress, pTex->SRV, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
		cpuCurrDescHandleAddress.Offset(1, srvDescriptorSize);
	}

	// --- 3) PSO/RS/DescHeap binding.
	pCommandList->SetPipelineState(pPSOManager->QueryPSO(m_pPSOHandle)->pPSO);
	pCommandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	pCommandList->IASetVertexBuffers(0, 1, &m_VertexBufferView);

	// --- 4) Root CBV binding
	pCommandList->SetGraphicsRootConstantBufferView(ROOT_SLOT_CBV_PER_DRAW, cb->pGPUMemAddr);

	// --- 5) TriGroup loop: t0가 가리키는 SRV를 매 드로우마다 바꿈
	CD3DX12_GPU_DESCRIPTOR_HANDLE gpuCurrDescHandleAddress = gpuDescriptorTable; // 첫 TriGroup의 t0
	for (UINT i = 0; i < m_NumTriGroups; ++i)
	{
		// SRV 테이블 루트 파라미터 바인딩 (t0 시작 핸들)
		pCommandList->SetGraphicsRootDescriptorTable(ROOT_SLOT_SRV_TABLE, gpuCurrDescHandleAddress);

		// 인덱스 버퍼/드로우
		const IndexedTriGroup& tg = m_pTriGroupList[i];
		pCommandList->IASetIndexBuffer(&tg.IndexBufferView);
		pCommandList->DrawIndexedInstanced(tg.NumTriangles * 3, 1, 0, 0, 0);

		// 다음 TriGroup의 SRV로 한 칸 이동 (t0만 쓰므로 1칸씩)
		gpuCurrDescHandleAddress.Offset(1, srvDescriptorSize);
	}
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
		{ "TANGENT",	0, DXGI_FORMAT_R32G32B32_FLOAT,	0, 20,	D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
	};
	static_assert(sizeof(Vertex) == 32, "BasicVertex was changed. Please update the input layout.");

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
		for (UINT i = 0; i < m_NumTriGroups; i++)
		{
			if (m_pTriGroupList[i].IndexBuffer)
			{
				m_pTriGroupList[i].IndexBuffer->Release();
				m_pTriGroupList[i].IndexBuffer = nullptr;
			}
			if (m_pTriGroupList[i].pTexHandle)
			{
				m_pRenderer->DeleteTexture(m_pTriGroupList[i].pTexHandle);
				m_pTriGroupList[i].pTexHandle = nullptr;
			}
		}
		delete[] m_pTriGroupList;
		m_pTriGroupList = nullptr;
	}

	if (m_pVertexBuffer)
	{
		m_pVertexBuffer->Release();
		m_pVertexBuffer = nullptr;
	}

	if (m_pPSOHandle)
	{
		PSOManager* pPsoManager = m_pRenderer->GetPSOManager();
		pPsoManager->ReleasePSO(m_pPSOHandle);
		m_pPSOHandle = nullptr;
	}
}