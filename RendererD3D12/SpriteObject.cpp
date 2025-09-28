#include "pch.h"
#include "D3D12ResourceManager.h"
#include "SimpleConstantBufferPool.h"
#include "SingleDescriptorAllocator.h"
#include "ShaderManager.h"
#include "DescriptorPool.h"
#include "D3D12Renderer.h"
#include "SpriteObject.h"

using namespace DirectX;

ID3D12RootSignature* SpriteObject::m_pRootSignature = nullptr;
ID3D12PipelineState* SpriteObject::m_pPipelineState = nullptr;

ID3D12Resource* SpriteObject::m_pVertexBuffer = nullptr;
D3D12_VERTEX_BUFFER_VIEW SpriteObject::m_VertexBufferView = {};

ID3D12Resource* SpriteObject::m_pIndexBuffer = nullptr;
D3D12_INDEX_BUFFER_VIEW SpriteObject::m_IndexBufferView = {};

int SpriteObject::m_InitRefCount = 0;

STDMETHODIMP SpriteObject::QueryInterface(REFIID refiid, void** ppv)
{
	return E_NOINTERFACE;
}

STDMETHODIMP_(ULONG) SpriteObject::AddRef()
{
	m_RefCount++;
	return m_RefCount;

}

STDMETHODIMP_(ULONG) SpriteObject::Release()
{
	DWORD	refCount = --m_RefCount;
	if (!refCount)
	{
		delete this;
	}
	return refCount;
}

bool SpriteObject::Initialize(D3D12Renderer* pRenderer)
{
	m_pRenderer = pRenderer;

	return initCommonResources();
}

bool SpriteObject::Initialize(D3D12Renderer* pRenderer, const WCHAR* wchTexFileName, const RECT* pRectOrNull)
{
	m_pRenderer = pRenderer;

	bool bResult = (initCommonResources() != 0);
	if (bResult)
	{
		UINT texWidth = 1;
		UINT texHeight = 1;
		m_pTexHandle = (TextureHandle*)m_pRenderer->CreateTextureFromFile(wchTexFileName);
		if (m_pTexHandle)
		{
			D3D12_RESOURCE_DESC	 desc = m_pTexHandle->pTexResource->GetDesc();
			texWidth = (UINT)desc.Width;
			texHeight = (UINT)desc.Height;
		}
		if (pRectOrNull)
		{
			m_Rect = *pRectOrNull;
			m_Scale.x = (float)(m_Rect.right - m_Rect.left) / (float)texWidth;
			m_Scale.y = (float)(m_Rect.bottom - m_Rect.top) / (float)texHeight;
		}
		else
		{
			if (m_pTexHandle)
			{
				D3D12_RESOURCE_DESC	 desc = m_pTexHandle->pTexResource->GetDesc();
				m_Rect.left = 0;
				m_Rect.top = 0;
				m_Rect.right = (LONG)desc.Width;
				m_Rect.bottom = (LONG)desc.Height;
			}
		}
	}
	return bResult;
}

void SpriteObject::Draw(int threadIndex, ID3D12GraphicsCommandList6* pCommandList, const XMFLOAT2* pPos, const XMFLOAT2* pScale, float Z)
{
	XMFLOAT2 Scale = { m_Scale.x * pScale->x, m_Scale.y * pScale->y };
	DrawWithTex(threadIndex, pCommandList, pPos, &Scale, &m_Rect, Z, m_pTexHandle);
}

void SpriteObject::DrawWithTex(int threadIndex, ID3D12GraphicsCommandList6* pCommandList, const XMFLOAT2* pPos, const XMFLOAT2* pScale, const RECT* pRect, float Z, TextureHandle* pTexHandle)
{
	// 각각의 draw()작업의 무결성을 보장하려면 draw() 작업마다 다른 영역의 descriptor table(shader visible)과 다른 영역의 CBV를 사용해야 한다.
	// 따라서 draw()할 때마다 CBV는 ConstantBuffer Pool로부터 할당받고, 렌더리용 descriptor table(shader visible)은 descriptor pool로부터 할당 받는다.

	ID3D12Device5* pD3DDeivce = m_pRenderer->GetD3DDevice();
	UINT srvDescriptorSize = m_pRenderer->GetSrvDescriptorSize();
	DescriptorPool* pDescriptorPool = m_pRenderer->GetDescriptorPool(threadIndex);
	ID3D12DescriptorHeap* pDescriptorHeap = pDescriptorPool->GetDescriptorHeap();
	SimpleConstantBufferPool* pConstantBufferPool = m_pRenderer->GetConstantBufferPool(CONSTANT_BUFFER_TYPE_SPRITE, threadIndex);

	UINT texWidth = 0;
	UINT texHeight = 0;

	D3D12_CPU_DESCRIPTOR_HANDLE srv = {};
	if (pTexHandle)
	{
		D3D12_RESOURCE_DESC desc = pTexHandle->pTexResource->GetDesc();
		texWidth = (UINT)desc.Width;
		texHeight = (UINT)desc.Height;
		srv = pTexHandle->SRV;
	}

	RECT rect = {};
	if (!pRect)
	{
		rect.left = 0;
		rect.top = 0;
		rect.right = texWidth;
		rect.bottom = texHeight;
		pRect = &rect;
	}

	CD3DX12_GPU_DESCRIPTOR_HANDLE gpuDescriptorTable = {};
	CD3DX12_CPU_DESCRIPTOR_HANDLE cpuDescriptorTable = {};

	if (!pDescriptorPool->AllocDescriptorTable(&cpuDescriptorTable, &gpuDescriptorTable, DESCRIPTOR_COUNT_FOR_DRAW))
	{
		ASSERT(false, "Failed to allocate descriptor table");
	}

	// 각각의 draw()에 대해 독립적인 constant buffer(내부적으로는 같은 resource의 다른 영역)를 사용한다.
	ConstantBufferContainer* pCB = pConstantBufferPool->Alloc();
	if (!pCB)
	{
		ASSERT(false, "Failed to allocate constant buffer");
	}
	ConstantBufferSprite* pConstantBufferSprite = (ConstantBufferSprite*)pCB->pSystemMemAddr;

	// constant buffer의 내용을 설정
	pConstantBufferSprite->ScreenResolution.x = (float)m_pRenderer->GetScreenWidth();
	pConstantBufferSprite->ScreenResolution.y = (float)m_pRenderer->GetScreenHeigt();
	pConstantBufferSprite->Position = *pPos;
	pConstantBufferSprite->Scale = *pScale;
	pConstantBufferSprite->TexSize.x = (float)texWidth;
	pConstantBufferSprite->TexSize.y = (float)texHeight;
	pConstantBufferSprite->TexSampePos.x = (float)pRect->left;
	pConstantBufferSprite->TexSampePos.y = (float)pRect->top;
	pConstantBufferSprite->TexSampleSize.x = (float)(pRect->right - pRect->left);
	pConstantBufferSprite->TexSampleSize.y = (float)(pRect->bottom - pRect->top);
	pConstantBufferSprite->Z = Z;
	pConstantBufferSprite->Alpha = 1.0f;

	// set RootSignature
	pCommandList->SetGraphicsRootSignature(m_pRootSignature);
	pCommandList->SetDescriptorHeaps(1, &pDescriptorHeap);

	// Descriptor Table 구성
	// 이번에 사용할 constant buffer의 descriptor를 렌더링용(shader visible) descriptor table에 카피

	CD3DX12_CPU_DESCRIPTOR_HANDLE cbvDest(cpuDescriptorTable, SPRITE_DESCRIPTOR_INDEX_CBV, srvDescriptorSize);
	pD3DDeivce->CopyDescriptorsSimple(1, cbvDest, pCB->CBVHandle, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
	if (srv.ptr)
	{
		CD3DX12_CPU_DESCRIPTOR_HANDLE srvDest(cpuDescriptorTable, SPRITE_DESCRIPTOR_INDEX_TEX, srvDescriptorSize);
		pD3DDeivce->CopyDescriptorsSimple(1, srvDest, srv, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
	}

	pCommandList->SetGraphicsRootDescriptorTable(0, gpuDescriptorTable);

	pCommandList->SetPipelineState(m_pPipelineState);
	pCommandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	pCommandList->IASetVertexBuffers(0, 1, &m_VertexBufferView);
	pCommandList->IASetIndexBuffer(&m_IndexBufferView);
	pCommandList->DrawIndexedInstanced(6, 1, 0, 0, 0);

}

SpriteObject::SpriteObject()
{

}

SpriteObject::~SpriteObject()
{ 
	m_pRenderer->EnsureCompleted(); 
	cleanup(); 
}

bool SpriteObject::initCommonResources()
{
	if (m_InitRefCount)
	{
		goto lb_true;
	}

	initRootSinagture();
	initPipelineState();
	initMesh();

lb_true:
	m_InitRefCount++;
	return m_InitRefCount;
}

void SpriteObject::cleanupSharedResources()
{
	if (!m_InitRefCount)
	{
		return;
	}

	int refCount = --m_InitRefCount;
	if (!refCount)
	{
		if (m_pRootSignature)
		{
			m_pRootSignature->Release();
			m_pRootSignature = nullptr;
		}
		if (m_pPipelineState)
		{
			m_pPipelineState->Release();
			m_pPipelineState = nullptr;
		}
		if (m_pVertexBuffer)
		{
			m_pVertexBuffer->Release();
			m_pVertexBuffer = nullptr;
		}
		if (m_pIndexBuffer)
		{
			m_pIndexBuffer->Release();
			m_pIndexBuffer = nullptr;
		}
	}
}

bool SpriteObject::initRootSinagture()
{
	HRESULT hr = S_OK;

	ID3D12Device5* pD3DDeivce = m_pRenderer->GetD3DDevice();
	ID3DBlob* pSignature = nullptr;
	ID3DBlob* pError = nullptr;

	CD3DX12_DESCRIPTOR_RANGE ranges[2] = {};
	ranges[0].Init(D3D12_DESCRIPTOR_RANGE_TYPE_CBV, 1, 0);	// b0 : Constant Buffer View
	ranges[1].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0);	// t0 : Shader Resource View(Tex)

	CD3DX12_ROOT_PARAMETER rootParameters[1] = {};
	rootParameters[0].InitAsDescriptorTable(_countof(ranges), ranges, D3D12_SHADER_VISIBILITY_ALL);

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

bool SpriteObject::initPipelineState()
{
	HRESULT hr = S_OK;

	ID3D12Device5* pD3DDeivce = m_pRenderer->GetD3DDevice();
	ShaderManager* pShaderManager = m_pRenderer->GetShaderManager();

	ShaderHandle* pVertexShader = pShaderManager->CreateShaderDXC(L"shSprite.hlsl", L"VSMain", L"vs_6_0", 0);
	ASSERT(pVertexShader, "Shader compilation failed.");

	ShaderHandle* pPixelShader = pShaderManager->CreateShaderDXC(L"shSprite.hlsl", L"PSMain", L"ps_6_0", 0);
	ASSERT(pPixelShader, "Shader compilation failed.");

	// Define the vertex input layout.
	D3D12_INPUT_ELEMENT_DESC inputElementDescs[] =
	{
		{ "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
		{ "COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
		{ "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT,	0, 28,	D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 }
	};
	static_assert(sizeof(SpriteVertex) == 36, "SpriteVertex was changed. Please update the input layout.");

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
	hr = pD3DDeivce->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&m_pPipelineState));
	ASSERT(SUCCEEDED(hr), "Failed to create pipeline state.");

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

bool SpriteObject::initMesh()
{
	// TODO: 바깥에서 버텍스데이터와 텍스처를 입력하는 식으로 변경할 것
	ID3D12Device5* pD3DDeivce = m_pRenderer->GetD3DDevice();
	UINT srvDescriptorSize = m_pRenderer->GetSrvDescriptorSize();
	D3D12ResourceManager*	pResourceManager = m_pRenderer->GetResourceManager();
	SingleDescriptorAllocator* pSingleDescriptorAllocator = m_pRenderer->GetSingleDescriptorAllocator();
	BOOL bUseGpuUploadHeaps = m_pRenderer->IsGpuUploadHeapsEnabledInl();

	// Create the vertex buffer.
	// Define the geometry for a triangle.
	SpriteVertex vertices[] =
	{
		{ { 0.0f, 1.0f, 0.0f }, { 1.0f, 1.0f, 1.0f, 1.0f }, { 0.0f, 1.0f } },
		{ { 0.0f, 0.0f, 0.0f }, { 1.0f, 1.0f, 1.0f, 1.0f }, { 0.0f, 0.0f } },
		{ { 1.0f, 0.0f, 0.0f }, { 1.0f, 1.0f, 1.0f, 1.0f }, { 1.0f, 0.0f } },
		{ { 1.0f, 1.0f, 0.0f }, { 1.0f, 1.0f, 1.0f, 1.0f }, { 1.0f, 1.0f } },
	};


	uint16_t indices[] =
	{
		0, 1, 2,
		0, 2, 3
	};

	const UINT vertexBufferSize = sizeof(vertices);
	bool bUseGpuUploadHepas = m_pRenderer->IsGpuUploadHeapsEnabledInl();
	if (FAILED(pResourceManager->CreateVertexBuffer(sizeof(SpriteVertex), (DWORD)_countof(vertices), &m_VertexBufferView, &m_pVertexBuffer, vertices, bUseGpuUploadHepas)))
	{
		ASSERT(false, "Failed to create vertex buffer");
		return false;
	}

	if (FAILED(pResourceManager->CreateIndexBuffer((DWORD)_countof(indices), &m_IndexBufferView, &m_pIndexBuffer, indices, bUseGpuUploadHeaps)))
	{
		ASSERT(false, "Failed to create index buffer");
		return false;
	}

	return true;
}

void SpriteObject::cleanup()
{
	if (m_pTexHandle)
	{
		m_pRenderer->DeleteTexture(m_pTexHandle);
		m_pTexHandle = nullptr;
	}
	cleanupSharedResources();
}