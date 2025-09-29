#include "pch.h"
#include "SkyObject.h"

#include "D3D12Renderer.h"
#include "D3D12ResourceManager.h"
#include "SimpleConstantBufferPool.h"
#include "DescriptorPool.h"
#include "SingleDescriptorAllocator.h"
#include "ShaderManager.h"

static float s_SunTheta = 0.0f;

bool SkyObject::Initialize(D3D12Renderer* pRenderer)
{
	m_pRenderer = pRenderer;
	if (!m_pRenderer) return false;

	if (!initRootSinagture())
	{
		ASSERT(false, "Sky: init root signature failed");
		return false;
	}
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
	DescriptorPool* pDescriptorPool = m_pRenderer->GetDescriptorPool(threadIndex);
	ID3D12DescriptorHeap* pDescriptorHeap = pDescriptorPool->GetDescriptorHeap();
	SimpleConstantBufferPool* pCBPool = m_pRenderer->GetConstantBufferPool(CONSTANT_BUFFER_TYPE_DEFAULT, threadIndex);
	const UINT srvDescSize = m_pRenderer->GetSrvDescriptorSize();

	CD3DX12_GPU_DESCRIPTOR_HANDLE gpuTable = {};
	CD3DX12_CPU_DESCRIPTOR_HANDLE cpuTable = {};
	const UINT kDescriptorCount = 2; // b0, b1
	if (!pDescriptorPool->AllocDescriptorTable(&cpuTable, &gpuTable, kDescriptorCount))
	{
		ASSERT(false, "Sky: descriptor table alloc failed");
		return;
	}

	// Constant Buffer (b0, b1)
	// b0: CB_SkyMatrices
	ConstantBufferContainer* cb0 = pCBPool->Alloc();
	ASSERT(cb0, "Sky: CB alloc b0 failed");
	// b1: CB_SkyParams
	ConstantBufferContainer* cb1 = pCBPool->Alloc();
	ASSERT(cb1, "Sky: CB alloc b1 failed");

	XMMATRIX view{}, proj{};
	m_pRenderer->GetViewProjMatrix(&view, &proj);

	XMFLOAT4X4 viewNoTransF{};
	XMStoreFloat4x4(&viewNoTransF, view);
	viewNoTransF._41 = viewNoTransF._42 = viewNoTransF._43 = 0.0f;
	viewNoTransF._14 = viewNoTransF._24 = viewNoTransF._34 = 0.0f;
	XMMATRIX vnt = XMLoadFloat4x4(&viewNoTransF);

	CB_SkyMatrices* pMat = reinterpret_cast<CB_SkyMatrices*>(cb0->pSystemMemAddr);
	XMStoreFloat4x4(&pMat->InvProj, XMMatrixInverse(nullptr, proj));
	XMStoreFloat4x4(&pMat->InvView, XMMatrixInverse(nullptr, vnt));

	s_SunTheta += 0.001f;
	float azimuth = s_SunTheta; 
	float elevation = 0.35f * sinf(s_SunTheta * 0.5f) + 0.35f;
	float cosEl = cosf(elevation);
	float sinEl = sinf(elevation);

	XMVECTOR sunToSky = XMVector3Normalize(XMVectorSet(cosEl * cosf(azimuth), sinEl, cosEl * sinf(azimuth), 0.0f));
	XMVECTOR sunDirToGround = XMVectorNegate(sunToSky);

	CB_SkyParams* pPar = reinterpret_cast<CB_SkyParams*>(cb1->pSystemMemAddr);
	XMStoreFloat3(&pPar->SunDirW, sunDirToGround);

	// Copy cbv to descriptor table.
	// b0
	CD3DX12_CPU_DESCRIPTOR_HANDLE dst0(cpuTable, 0, srvDescSize);
	pDevice->CopyDescriptorsSimple(1, dst0, cb0->CBVHandle, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
	// b1
	CD3DX12_CPU_DESCRIPTOR_HANDLE dst1(cpuTable, 1, srvDescSize);
	pDevice->CopyDescriptorsSimple(1, dst1, cb1->CBVHandle, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

	// Draw
	pCommandList->SetGraphicsRootSignature(m_pRootSignature);
	pCommandList->SetDescriptorHeaps(1, &pDescriptorHeap);
	pCommandList->SetPipelineState(m_pPipelineState);
	pCommandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	pCommandList->SetGraphicsRootDescriptorTable(0, gpuTable);
	pCommandList->DrawInstanced(3, 1, 0, 0);
}

void SkyObject::Cleanup()
{
	if (m_pVertexBuffer) { m_pVertexBuffer->Release();  m_pVertexBuffer = nullptr; }
	if (m_pIndexBuffer) { m_pIndexBuffer->Release();   m_pIndexBuffer = nullptr; }
	m_VertexBufferView = {};
	m_IndexBufferView = {};

	if (m_pPipelineState) { m_pPipelineState->Release(); m_pPipelineState = nullptr; }
	if (m_pRootSignature) { m_pRootSignature->Release(); m_pRootSignature = nullptr; }
}

bool SkyObject::initRootSinagture()
{
	HRESULT hr = S_OK;

	ID3D12Device5* pDevice = m_pRenderer->GetD3DDevice();
	ID3DBlob* pSignature = nullptr;
	ID3DBlob* pError = nullptr;

	CD3DX12_DESCRIPTOR_RANGE ranges[1] = {};
	ranges[0].Init(D3D12_DESCRIPTOR_RANGE_TYPE_CBV, 2, 0); // base b0, count=2 → b0,b1

	CD3DX12_ROOT_PARAMETER rootParams[1] = {};
	rootParams[0].InitAsDescriptorTable(1, ranges, D3D12_SHADER_VISIBILITY_ALL);

	D3D12_STATIC_SAMPLER_DESC sampler = {};
	SetDefaultSamplerDesc(&sampler, 0);
	sampler.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;

	D3D12_ROOT_SIGNATURE_FLAGS flags =
		D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT |
		D3D12_ROOT_SIGNATURE_FLAG_DENY_HULL_SHADER_ROOT_ACCESS |
		D3D12_ROOT_SIGNATURE_FLAG_DENY_DOMAIN_SHADER_ROOT_ACCESS |
		D3D12_ROOT_SIGNATURE_FLAG_DENY_GEOMETRY_SHADER_ROOT_ACCESS;

	CD3DX12_ROOT_SIGNATURE_DESC rsDesc;
	rsDesc.Init(_countof(rootParams), rootParams, 1, &sampler, flags);

	hr = D3D12SerializeRootSignature(&rsDesc, D3D_ROOT_SIGNATURE_VERSION_1, &pSignature, &pError);
	ASSERT(SUCCEEDED(hr), "Sky: RootSignature serialize failed");

	hr = pDevice->CreateRootSignature(0, pSignature->GetBufferPointer(), pSignature->GetBufferSize(), IID_PPV_ARGS(&m_pRootSignature));
	ASSERT(SUCCEEDED(hr), "Sky: CreateRootSignature failed");

	if (pSignature)
	{
		pSignature->Release();
	}
	if (pError)
	{
		pError->Release();
	}
	return true;
}

bool SkyObject::initPipelineState()
{
	HRESULT hr = S_OK;

	ID3D12Device5* pDevice = m_pRenderer->GetD3DDevice();
	ShaderManager* pShaderManager = m_pRenderer->GetShaderManager();

	// Sky.hlsl (VSMain/PSMain)
	ShaderHandle* pVertexShader = pShaderManager->CreateShaderDXC(L"ProceduralSky.hlsl", L"VSMain", L"vs_6_0", 0);
	ASSERT(pVertexShader, "Sky: VS compile failed");
	ShaderHandle* pPixelShader = pShaderManager->CreateShaderDXC(L"ProceduralSky.hlsl", L"PSMain", L"ps_6_0", 0);
	ASSERT(pPixelShader, "Sky: PS compile failed");

	D3D12_GRAPHICS_PIPELINE_STATE_DESC posDesc = {};
	posDesc.InputLayout = { nullptr, 0 };
	posDesc.pRootSignature = m_pRootSignature;
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

	hr = pDevice->CreateGraphicsPipelineState(&posDesc, IID_PPV_ARGS(&m_pPipelineState));
	ASSERT(SUCCEEDED(hr), "Sky: CreateGraphicsPipelineState failed");

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