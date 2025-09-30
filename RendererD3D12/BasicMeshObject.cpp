#include "pch.h"
#include "D3D12ResourceManager.h"
#include "SimpleConstantBufferPool.h"
#include "SingleDescriptorAllocator.h"
#include "ShaderManager.h"
#include "PSOManager.h"
#include "DescriptorPool.h"
#include "D3D12Renderer.h"
#include "BasicMeshObject.h"

using namespace DirectX;

ID3D12RootSignature* BasicMeshObject::m_pRootSignature = nullptr;

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

	initRootSinagture();
	initPipelineState();

	return bResult;
}

void BasicMeshObject::Draw(int threadIndex, ID3D12GraphicsCommandList6* pCommandList, const XMMATRIX* wordMatrix)
{
	// 각각의 draw()작업의 무결성을 보장하려면 draw() 작업마다 다른 영역의 descriptor table(shader visible)과 다른 영역의 CBV를 사용해야 한다.
	// 따라서 draw()할 때마다 CBV는 ConstantBuffer Pool로부터 할당받고, 렌더리용 descriptor table(shader visible)은 descriptor pool로부터 할당 받는다.

	ID3D12Device5* pD3DDeivce = m_pRenderer->GetD3DDevice();
	UINT srvDescriptorSize = m_pRenderer->GetSrvDescriptorSize();
	DescriptorPool* pDescriptorPool = m_pRenderer->GetDescriptorPool(threadIndex);
	ID3D12DescriptorHeap* pDescriptorHeap = pDescriptorPool->GetDescriptorHeap();
	SimpleConstantBufferPool* pConstantBufferPool = m_pRenderer->GetConstantBufferPool(CONSTANT_BUFFER_TYPE_DEFAULT, threadIndex);
	PSOManager* pPSOManager = m_pRenderer->GetPSOManager();

	CD3DX12_GPU_DESCRIPTOR_HANDLE gpuDescriptorTable = {};
	CD3DX12_CPU_DESCRIPTOR_HANDLE cpuDescriptorTable = {};
	UINT requiredDescriptorCount = static_cast<UINT>(DESCRIPTOR_COUNT_PER_OBJ + (m_NumTriGroups * DESCRIPTOR_COUNT_PER_TRI_GROUP));

	if (!pDescriptorPool->AllocDescriptorTable(&cpuDescriptorTable, &gpuDescriptorTable, requiredDescriptorCount))
	{
		ASSERT(false, "Failed to allocate descriptor table.");
	}

	// 각각의 draw()에 대해 독립적인 constant buffer(내부적으로는 같은 resource의 다른 영역)를 사용한다.
	ConstantBufferContainer* pCB = pConstantBufferPool->Alloc();
	ASSERT(pCB, "Failed to allocate constant buffer.");

	ConstantBufferDefault* pConstantBufferDefault = (ConstantBufferDefault*)pCB->pSystemMemAddr;

	// constant buffer의 내용을 설정
	// view/proj matrix
	m_pRenderer->GetViewProjMatrix(&pConstantBufferDefault->ViewMatrix, &pConstantBufferDefault->ProjMatrix);
	
	// world matrix
	pConstantBufferDefault->WorldMatrix = XMMatrixTranspose(*wordMatrix);

	// Descriptor Table 구성
	// 이번에 사용할 constant buffer의 descriptor를 렌더링용(shader visible) descriptor table에 카피

	// per Obj
	CD3DX12_CPU_DESCRIPTOR_HANDLE dest(cpuDescriptorTable, BASIC_MESH_DESCRIPTOR_INDEX_PER_OBJ_CBV, srvDescriptorSize);
	pD3DDeivce->CopyDescriptorsSimple(1, dest, pCB->CBVHandle, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);	// cpu측 코드에서는 cpu descriptor handle에만 write가능
	dest.Offset(1, srvDescriptorSize);

	// per tri-group
	for (UINT i = 0; i < m_NumTriGroups; i++)
	{
		IndexedTriGroup* pTriGroup = m_pTriGroupList + i;
		TextureHandle* pTexHandle = pTriGroup->pTexHandle;
		ASSERT(pTexHandle, "Texture not found.");
		if (pTexHandle)
		{
			pD3DDeivce->CopyDescriptorsSimple(1, dest, pTexHandle->SRV, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);	// cpu측 코드에서는 cpu descriptor handle에만 write가능
		}
		dest.Offset(1, srvDescriptorSize);
	}
	
	// set RootSignature
	pCommandList->SetGraphicsRootSignature(m_pRootSignature);
	pCommandList->SetDescriptorHeaps(1, &pDescriptorHeap);

	// ex) when TriGroupCount = 3
	// per OBJ | TriGroup 0 | TriGroup 1 | TriGroup 2 |
	// CBV     |     SRV    |     SRV    |     SRV    | 

	pCommandList->SetPipelineState(pPSOManager->QueryPSO(m_pPSOHandle)->pPSO);
	pCommandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	pCommandList->IASetVertexBuffers(0, 1, &m_VertexBufferView);

	// set descriptor table for root-param 0
	pCommandList->SetGraphicsRootDescriptorTable(0, gpuDescriptorTable);	// Entry per Obj

	CD3DX12_GPU_DESCRIPTOR_HANDLE gpuDescriptorTableForTriGroup(gpuDescriptorTable, DESCRIPTOR_COUNT_PER_OBJ, srvDescriptorSize);
	for (UINT i = 0; i < m_NumTriGroups; i++)
	{
		// set descriptor table for root-param 1
		pCommandList->SetGraphicsRootDescriptorTable(1, gpuDescriptorTableForTriGroup);	// Entry of Tri-Groups
		gpuDescriptorTableForTriGroup.Offset(1, srvDescriptorSize);

		IndexedTriGroup* pTriGroup = m_pTriGroupList + i;
		pCommandList->IASetIndexBuffer(&pTriGroup->IndexBufferView);
		pCommandList->DrawIndexedInstanced(pTriGroup->NumTriangles * 3, 1, 0, 0, 0);
	}
}

bool BasicMeshObject::initRootSinagture()
{
	if (m_pRootSignature)
	{
		return true;
	}

	HRESULT hr = S_OK;

	ID3D12Device5* pD3DDeivce = m_pRenderer->GetD3DDevice();
	ID3DBlob* pSignature = nullptr;
	ID3DBlob* pError = nullptr;

	// Object - CBV - RootParam(0)
	// {
	//   TriGrup 0 - SRV[0] - RootParam(1) - Draw()
	//   TriGrup 1 - SRV[1] - RootParam(1) - Draw()
	//   TriGrup 2 - SRV[2] - RootParam(1) - Draw()
	//   TriGrup 3 - SRV[3] - RootParam(1) - Draw()
	//   TriGrup 4 - SRV[4] - RootParam(1) - Draw()
	//   TriGrup 5 - SRV[5] - RootParam(1) - Draw()
	// }

	CD3DX12_DESCRIPTOR_RANGE rangesPerObj[1] = {};
	rangesPerObj[0].Init(D3D12_DESCRIPTOR_RANGE_TYPE_CBV, 1, 0);	// b0 : Constant Buffer View per Object

	CD3DX12_DESCRIPTOR_RANGE rangesPerTriGroup[1] = {};
	rangesPerTriGroup[0].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0);	// t0 : Shader Resource View(Tex) per Tri-Group

	CD3DX12_ROOT_PARAMETER rootParameters[2] = {};
	rootParameters[0].InitAsDescriptorTable(_countof(rangesPerObj), rangesPerObj, D3D12_SHADER_VISIBILITY_ALL);
	rootParameters[1].InitAsDescriptorTable(_countof(rangesPerTriGroup), rangesPerTriGroup, D3D12_SHADER_VISIBILITY_ALL);


	// default sampler
	D3D12_STATIC_SAMPLER_DESC sampler = {};
	SetDefaultSamplerDesc(&sampler, 0);
	sampler.Filter = D3D12_FILTER_MIN_MAG_MIP_POINT;

	// Allow input layout and deny uneccessary access to certain pipeline stages.
	D3D12_ROOT_SIGNATURE_FLAGS rootSignatureFlags =
		D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT |
		D3D12_ROOT_SIGNATURE_FLAG_DENY_HULL_SHADER_ROOT_ACCESS |
		D3D12_ROOT_SIGNATURE_FLAG_DENY_DOMAIN_SHADER_ROOT_ACCESS |
		D3D12_ROOT_SIGNATURE_FLAG_DENY_GEOMETRY_SHADER_ROOT_ACCESS |
		D3D12_ROOT_SIGNATURE_FLAG_DENY_PIXEL_SHADER_ROOT_ACCESS;

	// Create an empty root signature.
	CD3DX12_ROOT_SIGNATURE_DESC rootSignatureDesc = {};
	//rootSignatureDesc.Init(0, nullptr, 0, nullptr, D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);
	rootSignatureDesc.Init(_countof(rootParameters), rootParameters, 1, &sampler, D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);

	hr = D3D12SerializeRootSignature(&rootSignatureDesc, D3D_ROOT_SIGNATURE_VERSION_1, &pSignature, &pError);
	ASSERT(SUCCEEDED(hr), "Failed to serialize root signature.");

	hr = pD3DDeivce->CreateRootSignature(0, pSignature->GetBufferPointer(), pSignature->GetBufferSize(), IID_PPV_ARGS(&m_pRootSignature));
	ASSERT(SUCCEEDED(hr), "Failed to create root signature.");

	if (pSignature)
	{
		pSignature->Release();
		pSignature = nullptr;
	}
	if (pError)
	{
		pError->Release();
		pError = nullptr;
	}
	return true;
}

bool BasicMeshObject::initPipelineState()
{
	HRESULT hr = S_OK;

	ID3D12Device5* pD3DDeivce = m_pRenderer->GetD3DDevice();
	ShaderManager* pShaderManager = m_pRenderer->GetShaderManager();
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
	psoDesc.pRootSignature = m_pRootSignature;
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