#include "pch.h"
#include "SkyObject.h"

#include "D3D12Renderer.h"
#include "D3D12ResourceManager.h"
#include "SimpleConstantBufferPool.h"
#include "DescriptorPool.h"
#include "SingleDescriptorAllocator.h"
#include "ShaderManager.h"

// === 로컬 상태 ===
static float s_SunTheta = 0.0f; // Update에서 간단 회전(디버그)

bool SkyObject::Initialize(D3D12Renderer* pRenderer)
{
	m_pRenderer = pRenderer;
	if (!m_pRenderer) return false;

	if (!initRootSinagture())    return false;
	if (!initPipelineState())    return false;

	return true;
}

void SkyObject::Update(float dt)
{
	// 태양을 천천히 회전시켜 변화 확인(원치 않으면 제거)
	s_SunTheta += dt * 0.1f;
}

void SkyObject::Draw(int threadIndex, ID3D12GraphicsCommandList6* pCommandList)
{
	if (!pCommandList || !m_pRenderer || !m_pRootSignature || !m_pPipelineState)
		return;

	ID3D12Device5* pDevice = m_pRenderer->GetD3DDevice();
	DescriptorPool* pDescriptorPool = m_pRenderer->GetDescriptorPool(threadIndex);
	ID3D12DescriptorHeap* pDescriptorHeap = pDescriptorPool->GetDescriptorHeap();
	SimpleConstantBufferPool* pCBPool = m_pRenderer->GetConstantBufferPool(CONSTANT_BUFFER_TYPE_DEFAULT, threadIndex);
	const UINT                   srvDescSize = m_pRenderer->GetSrvDescriptorSize();

	// ---- Descriptor Table 2 슬롯(CBV2개) 확보 ----
	CD3DX12_GPU_DESCRIPTOR_HANDLE gpuTable{};
	CD3DX12_CPU_DESCRIPTOR_HANDLE cpuTable{};
	const UINT kDescriptorCount = 2; // b0, b1
	if (!pDescriptorPool->AllocDescriptorTable(&cpuTable, &gpuTable, kDescriptorCount))
	{
		ASSERT(false, "Sky: descriptor table alloc failed");
		return;
	}

	// ---- Constant Buffer 2개(b0, b1) 할당 & 채움 ----
	// b0: CB_SkyMatrices
	ConstantBufferContainer* cb0 = pCBPool->Alloc();
	ASSERT(cb0, "Sky: CB alloc b0 failed");
	// b1: CB_SkyParams
	ConstantBufferContainer* cb1 = pCBPool->Alloc();
	ASSERT(cb1, "Sky: CB alloc b1 failed");

	// View/Proj 가져오기
	XMMATRIX view{}, proj{};
	m_pRenderer->GetViewProjMatrix(&view, &proj);

	// view에서 translation 제거
	XMFLOAT4X4 viewNoTransF{};
	XMStoreFloat4x4(&viewNoTransF, view);
	viewNoTransF._41 = viewNoTransF._42 = viewNoTransF._43 = 0.0f;
	viewNoTransF._14 = viewNoTransF._24 = viewNoTransF._34 = 0.0f;
	XMMATRIX Vnt = XMLoadFloat4x4(&viewNoTransF);

	// invProj / invView(회전만)
	CB_SkyMatrices* pMat = reinterpret_cast<CB_SkyMatrices*>(cb0->pSystemMemAddr);
	XMStoreFloat4x4(&pMat->InvProj, XMMatrixInverse(nullptr, proj));
	XMStoreFloat4x4(&pMat->InvView, XMMatrixInverse(nullptr, Vnt));

	// 태양 방향(간단 회전)
	s_SunTheta += 0.001f;
	// azimuth: 0=+X, π/2=+Z (수평 회전)
	// elevation: 0=지평선, +위쪽 (라디안)
	float azimuth = s_SunTheta;          // 시간에 따라 증가
	float elevation = 0.35f * sinf(s_SunTheta * 0.5f) + 0.35f;
	// ↑ 예: 약 ~[-0.0, 0.7] 범위로 오르내리게 (원하는 범위로 조절)

	float cosEl = cosf(elevation);
	float sinEl = sinf(elevation);

	// "지면 → 태양" 방향 (하늘을 향함)
	XMVECTOR sunToSky = XMVector3Normalize(
		XMVectorSet(cosEl * cosf(azimuth), sinEl, cosEl * sinf(azimuth), 0.0f));

	// 셰이더는 "태양 → 지면(빛 진행)" 방향을 기대하므로 부호 반전
	XMVECTOR sunDirToGround = XMVectorNegate(sunToSky);

	CB_SkyParams* pPar = reinterpret_cast<CB_SkyParams*>(cb1->pSystemMemAddr);
	XMStoreFloat3(&pPar->SunDirW, sunDirToGround);

	// ---- 디스크립터 테이블에 CBV 2개를 카피(b0, b1 순서) ----
	// b0
	CD3DX12_CPU_DESCRIPTOR_HANDLE dst0(cpuTable, 0, srvDescSize);
	pDevice->CopyDescriptorsSimple(1, dst0, cb0->CBVHandle, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
	// b1
	CD3DX12_CPU_DESCRIPTOR_HANDLE dst1(cpuTable, 1, srvDescSize);
	pDevice->CopyDescriptorsSimple(1, dst1, cb1->CBVHandle, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

	// ---- 파이프라인 바인딩 & 드로우 ----
	pCommandList->SetGraphicsRootSignature(m_pRootSignature);
	pCommandList->SetDescriptorHeaps(1, &pDescriptorHeap);

	pCommandList->SetPipelineState(m_pPipelineState);
	pCommandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

	// 루트-파라미터 #0 = (CBV2개 테이블: b0, b1)
	pCommandList->SetGraphicsRootDescriptorTable(0, gpuTable);

	// 풀스크린 삼각형 (VB/IB 없음)
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
	ID3D12Device5* pDevice = m_pRenderer->GetD3DDevice();
	if (!pDevice) return false;

	// RootParam(0): DescriptorTable(CBV 2개: b0, b1)
	CD3DX12_DESCRIPTOR_RANGE ranges[1] = {};
	ranges[0].Init(D3D12_DESCRIPTOR_RANGE_TYPE_CBV, 2, 0); // base b0, count=2 → b0,b1

	CD3DX12_ROOT_PARAMETER rootParams[1] = {};
	rootParams[0].InitAsDescriptorTable(1, ranges, D3D12_SHADER_VISIBILITY_ALL);

	// (필요시) 정적 샘플러 – 이 셰이더에선 사용하지 않지만, 슬롯 0에 point sampler 예시
	D3D12_STATIC_SAMPLER_DESC sampler = {};
	SetDefaultSamplerDesc(&sampler, 0);
	sampler.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;

	// RS 플래그: IA 입력 레이아웃 허용 (입력 레이아웃은 실제로는 없음)
	D3D12_ROOT_SIGNATURE_FLAGS flags =
		D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT |
		D3D12_ROOT_SIGNATURE_FLAG_DENY_HULL_SHADER_ROOT_ACCESS |
		D3D12_ROOT_SIGNATURE_FLAG_DENY_DOMAIN_SHADER_ROOT_ACCESS |
		D3D12_ROOT_SIGNATURE_FLAG_DENY_GEOMETRY_SHADER_ROOT_ACCESS;
	// 픽셀 셰이더는 쓰므로 DENY_PIXEL_SHADER_* 는 넣지 않음

	CD3DX12_ROOT_SIGNATURE_DESC rsDesc;
	rsDesc.Init(_countof(rootParams), rootParams, 1, &sampler, flags);

	ID3DBlob* pSig = nullptr;
	ID3DBlob* pErr = nullptr;
	HRESULT hr = D3D12SerializeRootSignature(&rsDesc, D3D_ROOT_SIGNATURE_VERSION_1, &pSig, &pErr);
	ASSERT(SUCCEEDED(hr), "Sky: RootSignature serialize failed");
	hr = pDevice->CreateRootSignature(0, pSig->GetBufferPointer(), pSig->GetBufferSize(), IID_PPV_ARGS(&m_pRootSignature));
	ASSERT(SUCCEEDED(hr), "Sky: CreateRootSignature failed");

	if (pSig)  pSig->Release();
	if (pErr)  pErr->Release();
	return true;
}

bool SkyObject::initPipelineState()
{
	ID3D12Device5* pDevice = m_pRenderer->GetD3DDevice();
	ShaderManager* pSM = m_pRenderer->GetShaderManager();
	ASSERT(pDevice && pSM, "Sky: device or ShaderManager null");

	// Sky.hlsl (VSMain/PSMain)
	ShaderHandle* pVS = pSM->CreateShaderDXC(L"ProceduralSky.hlsl", L"VSMain", L"vs_6_0", 0);
	ASSERT(pVS, "Sky: VS compile failed");
	ShaderHandle* pPS = pSM->CreateShaderDXC(L"ProceduralSky.hlsl", L"PSMain", L"ps_6_0", 0);
	ASSERT(pPS, "Sky: PS compile failed");

	D3D12_GRAPHICS_PIPELINE_STATE_DESC desc = {};
	desc.pRootSignature = m_pRootSignature;
	desc.VS = CD3DX12_SHADER_BYTECODE(pVS->CodeBuffer, pVS->CodeSize);
	desc.PS = CD3DX12_SHADER_BYTECODE(pPS->CodeBuffer, pPS->CodeSize);

	// 입력 레이아웃 없음 (SV_VertexID)
	desc.InputLayout = { nullptr, 0 };
	desc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;

	// 래스터/블렌드/깊이
	desc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
	desc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;

	desc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);

	desc.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
	desc.DepthStencilState.DepthEnable = TRUE;
	desc.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;   // 하늘이 깊이 덮지 않도록
	desc.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;
	//desc.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_ALWAYS;

	desc.SampleMask = UINT_MAX;

	// 포맷 (엔진 전역 포맷을 쓰려면 Get 메서드로 가져오세요)
	// TODO: 필요 시 m_pRenderer->GetSwapChainRTVFormat(), GetDSVFormat() 등 사용
	desc.NumRenderTargets = 1;
	desc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM; // ← 엔진 포맷에 맞게 바꾸세요
	desc.DSVFormat = DXGI_FORMAT_D32_FLOAT;       // ← 엔진 포맷에 맞게 바꾸세요
	desc.SampleDesc.Count = 1;

	HRESULT hr = pDevice->CreateGraphicsPipelineState(&desc, IID_PPV_ARGS(&m_pPipelineState));
	ASSERT(SUCCEEDED(hr), "Sky: CreateGraphicsPipelineState failed");

	if (pVS) pSM->ReleaseShader(pVS);
	if (pPS) pSM->ReleaseShader(pPS);
	return true;
}