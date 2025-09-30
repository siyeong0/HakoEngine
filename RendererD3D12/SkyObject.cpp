#include "pch.h"
#include "SkyObject.h"
#include "D3D12Renderer.h"
#include "D3D12ResourceManager.h"
#include "SimpleConstantBufferPool.h"
#include "DescriptorPool.h"
#include "RootSignatureManager.h"
#include "PSOManager.h"
#include "SingleDescriptorAllocator.h"
#include "ShaderManager.h"

static float s_SunTheta = 0.0f;

bool SkyObject::Initialize(D3D12Renderer* pRenderer)
{
	m_pRenderer = pRenderer;
	if (!m_pRenderer) return false;

	if (!initPipelineState())
	{
		ASSERT(false, "Sky: init pipeline state failed");
		return false;
	}

	return true;
}

void SkyObject::Draw(int threadIndex, ID3D12GraphicsCommandList6* pCommandList)
{
	ID3D12Device5* pDevice = m_pRenderer->GetD3DDevice();
	SimpleConstantBufferPool* pConstantBufferPool = m_pRenderer->GetConstantBufferPool(CONSTANT_BUFFER_TYPE_DEFAULT, threadIndex);
	RootSignatureManager* pRootSigatureManager = m_pRenderer->GetRootSignatureManager();
	PSOManager* pPSOManager = m_pRenderer->GetPSOManager();

	// Constant Buffer (b0 = CB_SkyMatrices, b1 = CB_SkyParams)
	ConstantBufferContainer* cb0 = pConstantBufferPool->Alloc();
	ASSERT(cb0, "Sky: CB alloc b0 failed");
	ConstantBufferContainer* cb1 = pConstantBufferPool->Alloc();
	ASSERT(cb1, "Sky: CB alloc b1 failed");

	XMMATRIX view{}, proj{};
	m_pRenderer->GetViewProjMatrix(&view, &proj);
	XMFLOAT4X4 viewNoTransF{};
	XMStoreFloat4x4(&viewNoTransF, view);
	// translation 제거 (행렬 저장 규약에 맞춰 translation 성분만 0)
	viewNoTransF._41 = viewNoTransF._42 = viewNoTransF._43 = 0.0f;
	XMMATRIX vnt = XMLoadFloat4x4(&viewNoTransF);
	// b0: 역행렬(Transpose 포함: HLSL에서 row-vector mul 사용시)
	CB_SkyMatrices* pMat = reinterpret_cast<CB_SkyMatrices*>(cb0->pSystemMemAddr);
	XMMATRIX invProj = XMMatrixInverse(nullptr, proj);
	XMMATRIX invView = XMMatrixInverse(nullptr, vnt);
	XMStoreFloat4x4(&pMat->InvProj, XMMatrixTranspose(invProj));
	XMStoreFloat4x4(&pMat->InvView, XMMatrixTranspose(invView));
	// b1: 태양 방향 등 파라미터
	s_SunTheta += 0.001f;
	float azimuth = s_SunTheta;
	float elevation = 0.35f * sinf(s_SunTheta * 0.5f) + 0.35f;
	float cosEl = cosf(elevation), sinEl = sinf(elevation);

	XMVECTOR sunToSky = XMVector3Normalize(XMVectorSet(cosEl * cosf(azimuth), sinEl, cosEl * sinf(azimuth), 0.0f));
	XMVECTOR sunDirToGround = XMVectorNegate(sunToSky);

	CB_SkyParams* pPar = reinterpret_cast<CB_SkyParams*>(cb1->pSystemMemAddr);
	XMStoreFloat3(&pPar->SunDirW, sunDirToGround);

	// ---- RS/PSO/IA
	pCommandList->SetGraphicsRootSignature(pRootSigatureManager->Query(ERootSignatureType::GraphicsDefault));
	pCommandList->SetPipelineState(pPSOManager->QueryPSO(m_pPSOHandle)->pPSO);
	pCommandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

	// ---- CB binding (RS 슬롯: b0=0, b1=1, SRV 테이블=2)
	pCommandList->SetGraphicsRootConstantBufferView(/*slot b0*/0, cb0->pGPUMemAddr);
	pCommandList->SetGraphicsRootConstantBufferView(/*slot b1*/1, cb1->pGPUMemAddr);

	pCommandList->DrawInstanced(3, 1, 0, 0);
}

void SkyObject::Cleanup()
{
	if (m_pVertexBuffer) 
	{ 
		m_pVertexBuffer->Release(); 
		m_pVertexBuffer = nullptr; 
	}
	if (m_pIndexBuffer) 
	{ 
		m_pIndexBuffer->Release();  
		m_pIndexBuffer = nullptr; }
	m_VertexBufferView = {};
	m_IndexBufferView = {};

	if (m_pPSOHandle) 
	{ 
		PSOManager* pPSOManager = m_pRenderer->GetPSOManager();
		pPSOManager->ReleasePSO(m_pPSOHandle);
		m_pPSOHandle = nullptr;
	}
}

bool SkyObject::initPipelineState()
{
	HRESULT hr = S_OK;

	ID3D12Device5* pDevice = m_pRenderer->GetD3DDevice();
	ShaderManager* pShaderManager = m_pRenderer->GetShaderManager();
	RootSignatureManager* pRootSigManager = m_pRenderer->GetRootSignatureManager();
	PSOManager* pPSOManager = m_pRenderer->GetPSOManager();

	// Sky.hlsl (VSMain/PSMain)
	ShaderHandle* pVertexShader = pShaderManager->CreateShaderDXC(L"ProceduralSky.hlsl", L"VSMain", L"vs_6_0", 0);
	ASSERT(pVertexShader, "Sky: VS compile failed");
	ShaderHandle* pPixelShader = pShaderManager->CreateShaderDXC(L"ProceduralSky.hlsl", L"PSMain", L"ps_6_0", 0);
	ASSERT(pPixelShader, "Sky: PS compile failed");

	D3D12_GRAPHICS_PIPELINE_STATE_DESC posDesc = {};
	posDesc.InputLayout = { nullptr, 0 };
	posDesc.pRootSignature = pRootSigManager->Query(ERootSignatureType::GraphicsDefault);
	posDesc.VS = CD3DX12_SHADER_BYTECODE(pVertexShader->CodeBuffer, pVertexShader->CodeSize);
	posDesc.PS = CD3DX12_SHADER_BYTECODE(pPixelShader->CodeBuffer, pPixelShader->CodeSize);
	posDesc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
	posDesc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
	posDesc.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
	posDesc.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;   // 하늘이 깊이 덮지 않도록
	posDesc.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;
	posDesc.DepthStencilState.DepthEnable = TRUE;
	posDesc.SampleMask = UINT_MAX;
	posDesc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
	posDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
	posDesc.NumRenderTargets = 1;
	posDesc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
	posDesc.DSVFormat = DXGI_FORMAT_D32_FLOAT;
	posDesc.SampleDesc.Count = 1;

	m_pPSOHandle = pPSOManager->CreatePSO(posDesc, "Sky");
	ASSERT(m_pPSOHandle, "Sky: CreatePSO failed");

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