#include "pch.h"
#include "D3D12ResourceManager.h"
#include "SimpleConstantBufferPool.h"
#include "SingleDescriptorAllocator.h"
#include "ShaderManager.h"
#include "RootSignatureManager.h"
#include "PSOManager.h"
#include "DescriptorPool.h"
#include "D3D12Renderer.h"
#include "SpriteObject.h"

using namespace DirectX;

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

	bool bResult = false;
	
	bResult = (initCommonResources() != 0);
	ASSERT(bResult, "Failed to initialize common resources.");

	bResult = initPipelineState();
	ASSERT(bResult, "Failed to initialize pipeline state.");

	return bResult;
}

bool SpriteObject::Initialize(D3D12Renderer* pRenderer, const WCHAR* wchTexFileName, const RECT* pRectOrNull)
{
	bool bResult = Initialize(pRenderer);
	ASSERT(bResult, "Failed to initialize sprite object.");

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
	ID3D12Device5* pDevice = m_pRenderer->GetD3DDevice();
	UINT srvDescriptorSize = m_pRenderer->GetSrvDescriptorSize();
	DescriptorPool* pDescriptorPool = m_pRenderer->GetDescriptorPool(threadIndex);
	SimpleConstantBufferPool* pConstantBufferPool = m_pRenderer->GetConstantBufferPool(CONSTANT_BUFFER_TYPE_SPRITE, threadIndex);
	PSOManager* pPSOManager = m_pRenderer->GetPSOManager();

	// Texture information
	UINT texWidth = 0;
	UINT texHeight = 0;
	D3D12_CPU_DESCRIPTOR_HANDLE srv = {};
	if (pTexHandle)
	{
		D3D12_RESOURCE_DESC desc = pTexHandle->pTexResource->GetDesc();
		texWidth = static_cast<UINT>(desc.Width);
		texHeight = static_cast<UINT>(desc.Height);
		srv = pTexHandle->SRV;
	}
	// Sample region
	RECT rect = {};
	if (!pRect)
	{
		rect.left = 0;
		rect.top = 0;
		rect.right = texWidth;
		rect.bottom = texHeight;
		pRect = &rect;
	}

	// Root CBV binding
	ConstantBufferContainer* pCB = pConstantBufferPool->Alloc();
	ASSERT(pCB, "Failed to allocate constant buffer.");
	CONSTANT_BUFFER_SPRITE_OBJECT* pCBPerDraw = reinterpret_cast<CONSTANT_BUFFER_SPRITE_OBJECT*>(pCB->pSystemMemAddr);
	
	pCBPerDraw->ScreenResolution.x = (float)m_pRenderer->GetScreenWidth();
	pCBPerDraw->ScreenResolution.y = (float)m_pRenderer->GetScreenHeight();
	pCBPerDraw->Position = *pPos;
	pCBPerDraw->Scale = *pScale;
	pCBPerDraw->TexSize.x = (float)texWidth;
	pCBPerDraw->TexSize.y = (float)texHeight;
	pCBPerDraw->TexSampePos.x = (float)pRect->left;
	pCBPerDraw->TexSampePos.y = (float)pRect->top;
	pCBPerDraw->TexSampleSize.x = (float)(pRect->right - pRect->left);
	pCBPerDraw->TexSampleSize.y = (float)(pRect->bottom - pRect->top);
	pCBPerDraw->Z = Z;
	pCBPerDraw->Alpha = 1.0f;

	// SRV Descriptor table (1개)
	CD3DX12_CPU_DESCRIPTOR_HANDLE cpuTable{};
	CD3DX12_GPU_DESCRIPTOR_HANDLE gpuTable{};
	const UINT requiredSrvCount = 1;
	bool bOk = pDescriptorPool->AllocDescriptorTable(&cpuTable, &gpuTable, requiredSrvCount);
	ASSERT(bOk, "Failed to allocate descriptor table.");

	if (srv.ptr) 
	{
		pDevice->CopyDescriptorsSimple(1, cpuTable, srv, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
	}
	else 
	{
		// TODO: 더미(1x1 white) SRV가 있다면 여기서 복사
		// pDevice->CopyDescriptorsSimple(1, cpuTable, g_DefaultWhiteSRV, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
	}

	// Pipline/Input assembler state
	pCommandList->SetPipelineState(pPSOManager->QueryPSO(m_pPSOHandle)->pPSO);
	pCommandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	pCommandList->IASetVertexBuffers(0, 1, &m_VertexBufferView);
	pCommandList->IASetIndexBuffer(&m_IndexBufferView);
	// b1: per-draw
	pCommandList->SetGraphicsRootConstantBufferView(ROOT_SLOT_CBV_PER_DRAW, pCB->pGPUMemAddr);
	// t0: texture
	pCommandList->SetGraphicsRootDescriptorTable(ROOT_SLOT_SRV_TABLE, gpuTable);
	// Draw
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

bool SpriteObject::initPipelineState()
{
	HRESULT hr = S_OK;

	ID3D12Device5* pD3DDeivce = m_pRenderer->GetD3DDevice();
	ShaderManager* pShaderManager = m_pRenderer->GetShaderManager();
	RootSignatureManager* pRootSignatureManager = m_pRenderer->GetRootSignatureManager();
	PSOManager* pPSOManager = m_pRenderer->GetPSOManager();

	ShaderHandle* pVertexShader = pShaderManager->CreateShaderDXC(L"Sprite.hlsl", L"VSMain", L"vs_6_0", 0);
	ASSERT(pVertexShader, "Shader compilation failed.");

	ShaderHandle* pPixelShader = pShaderManager->CreateShaderDXC(L"Sprite.hlsl", L"PSMain", L"ps_6_0", 0);
	ASSERT(pPixelShader, "Shader compilation failed.");

	// Define the vertex input layout.
	D3D12_INPUT_ELEMENT_DESC inputElementDescs[] =
	{
		{ "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
		{ "COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
		{ "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT,	0, 28,	D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 }
	};
	static_assert(sizeof(SpriteVertex) == 36, "SpriteVertex was changed. Please update the input layout.");

	// TODO: sroting
	// Describe and create the blend state.
	D3D12_BLEND_DESC blendDesc = {};
	blendDesc.AlphaToCoverageEnable = FALSE;
	blendDesc.IndependentBlendEnable = FALSE;

	D3D12_RENDER_TARGET_BLEND_DESC rtBlendDesc = {};
	rtBlendDesc.BlendEnable = TRUE;                        // 블렌딩 켬
	rtBlendDesc.LogicOpEnable = FALSE;
	rtBlendDesc.SrcBlend = D3D12_BLEND_SRC_ALPHA;          // 소스 알파
	rtBlendDesc.DestBlend = D3D12_BLEND_INV_SRC_ALPHA;     // (1 - 알파)
	rtBlendDesc.BlendOp = D3D12_BLEND_OP_ADD;              // 합산
	rtBlendDesc.SrcBlendAlpha = D3D12_BLEND_ONE;           // 알파값은 그대로 유지
	rtBlendDesc.DestBlendAlpha = D3D12_BLEND_ZERO;
	rtBlendDesc.BlendOpAlpha = D3D12_BLEND_OP_ADD;
	rtBlendDesc.RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
	blendDesc.RenderTarget[0] = rtBlendDesc;

	// Describe and create the graphics pipeline state object (PSO).
	D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = {};
	psoDesc.InputLayout = { inputElementDescs, _countof(inputElementDescs) };
	psoDesc.pRootSignature = pRootSignatureManager->Query(ERootSignatureType::GraphicsDefault);
	psoDesc.VS = CD3DX12_SHADER_BYTECODE(pVertexShader->CodeBuffer, pVertexShader->CodeSize);
	psoDesc.PS = CD3DX12_SHADER_BYTECODE(pPixelShader->CodeBuffer, pPixelShader->CodeSize);
	psoDesc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
	// psoDesc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
	psoDesc.BlendState = blendDesc;
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

	m_pPSOHandle = pPSOManager->CreatePSO(psoDesc, "Sprite");
	ASSERT(m_pPSOHandle, "Failed to create PSO.");

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
	if (m_pPSOHandle)
	{
		PSOManager* pPSOManager = m_pRenderer->GetPSOManager();
		pPSOManager->ReleasePSO(m_pPSOHandle);
		m_pPSOHandle = nullptr;
	}
	cleanupSharedResources();
}