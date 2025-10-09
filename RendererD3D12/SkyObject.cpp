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

bool SkyObject::Initialize(D3D12Renderer* pRenderer)
{
	m_pRenderer = pRenderer;
	if (!m_pRenderer) return false;

	if (!initPipelineState())
	{
		ASSERT(false, "Sky: init pipeline state failed");
		return false;
	}

	m_pTransmittanceTex = (TextureHandle*)m_pRenderer->CreateTextureFromFile(L"./Resources/Atmos/Transmittance.dds");
	m_pScatteringTex = (TextureHandle*)m_pRenderer->CreateTextureFromFile(L"./Resources/Atmos/Scattering.dds");
	m_pIrradianceTex = (TextureHandle*)m_pRenderer->CreateTextureFromFile(L"./Resources/Atmos/Irradiance.dds");

	return true;
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
		m_pIndexBuffer = nullptr;
	}
	m_VertexBufferView = {};
	m_IndexBufferView = {};

	if (m_pTransmittanceTex)
	{
		m_pRenderer->DeleteTexture(m_pTransmittanceTex);
		m_pTransmittanceTex = nullptr;
	}
	if (m_pScatteringTex)
	{
		m_pRenderer->DeleteTexture(m_pScatteringTex);
		m_pScatteringTex = nullptr;
	}
	if (m_pIrradianceTex)
	{
		m_pRenderer->DeleteTexture(m_pIrradianceTex);
		m_pIrradianceTex = nullptr;
	}

	if (m_pPSOHandle)
	{
		PSOManager* pPSOManager = m_pRenderer->GetPSOManager();
		pPSOManager->ReleasePSO(m_pPSOHandle);
		m_pPSOHandle = nullptr;
	}
}


void SkyObject::Draw(int threadIndex, ID3D12GraphicsCommandList6* pCommandList)
{
	ID3D12Device5* pDevice = m_pRenderer->GetD3DDevice();
	UINT srvDescriptorSize = m_pRenderer->GetSrvDescriptorSize();
	DescriptorPool* pDescriptorPool = m_pRenderer->GetDescriptorPool(threadIndex);
	SimpleConstantBufferPool* pCameraConstantBufferPool = m_pRenderer->GetConstantBufferPool(CONSTANT_BUFFER_TYPE_PER_FRAME, threadIndex);
	SimpleConstantBufferPool* pAtmosConstantBufferPool = m_pRenderer->GetConstantBufferPool(CONSTANT_BUFFER_TYPE_ATMOS_CONSTANTS, threadIndex);
	RootSignatureManager* pRootSigatureManager = m_pRenderer->GetRootSignatureManager();

	// Constant Buffer (b0 = CB_SkyMatrices, b1 = CB_SkyParams)
	ConstantBufferContainer* cb0 = pCameraConstantBufferPool->Alloc();
	ASSERT(cb0, "Sky: CB alloc b0 failed");
	ConstantBufferContainer* cb1 = pAtmosConstantBufferPool->Alloc();
	ASSERT(cb1, "Sky: CB alloc b1 failed");

	// b0: Per-frame matrices
	CONSTANT_BUFFER_PER_FRAME* pCBPerFrame = (CONSTANT_BUFFER_PER_FRAME*)cb0->pSystemMemAddr;
	const CONSTANT_BUFFER_PER_FRAME& srcCBData = m_pRenderer->GetFrameCBData();
	std::memcpy(pCBPerFrame, &srcCBData, sizeof(CONSTANT_BUFFER_PER_FRAME));

	// b1: Atmospheric parameters
	CONSTANT_BUFFER_ATMOS* atmosCB = reinterpret_cast<CONSTANT_BUFFER_ATMOS*>(cb1->pSystemMemAddr);
	
	enum EAtmospherePresets
	{
		AtmosPreset_NoonClearSky = 0,
		AtmosPreset_AfternoonClearSky,
		AtmosPreset_GoldenHour,
		AtmosPreset_WinterSky,
		AtmosPreset_HazyDay,
		AtmosPreset_BlueHour,
		AtmosPreset_HighAltitude,
		AtmosPreset_ExtraSolar
	};
	const int ATMOS_PRESET = AtmosPreset_NoonClearSky;
	switch (ATMOS_PRESET)
	{
	case AtmosPreset_NoonClearSky:
		// 1) 정오 맑음(밝고 푸른 하늘)
		//	태양 고도 : 70°, 방위 : -Z
		//	인상 : 채도가 높은 푸른 하늘, 강한 하이라이트
		atmosCB->CameraPosPlanetCoord	= XMFLOAT3(0.0f, 6360005.0f, 0.0f); // 지상+5m
		atmosCB->SunDir				= XMFLOAT3(0.0f, -0.9397f, -0.3420f); // Sun→Ground
		atmosCB->SunExposure		= 14.0f;
		atmosCB->SunIrradiance		= XMFLOAT3(1.0f, 1.0f, 1.0f);
		atmosCB->MieG				= 0.80f;
		atmosCB->MieTint			= XMFLOAT3(0.92f, 0.92f, 0.98f);
		break;
	case AtmosPreset_AfternoonClearSky:
		// 2) 오후 3시 맑음 (밝지만 약간 웜)
		//	태양 고도: 35°, 방위: -Z
		//	인상: 살짝 따뜻한 톤, 그림자 길어짐
		atmosCB->CameraPosPlanetCoord = XMFLOAT3(0.0f, 6360002.0f, 0.0f);
		atmosCB->SunDir				= XMFLOAT3(0.0f, -0.5736f, -0.8192f);
		atmosCB->SunExposure		= 12.0f;
		atmosCB->SunIrradiance		= XMFLOAT3(1.05f, 1.0f, 0.95f);
		atmosCB->MieG				= 0.82f;
		atmosCB->MieTint			= XMFLOAT3(0.93f, 0.93f, 0.97f);
		break;
	case AtmosPreset_GoldenHour:
		// 3) 해질녘 골든아워 (웜/오렌지 하늘, 스카이라인 강조)
		//	태양 고도: 10°, 방위: -Z
		//	인상: 노을빛, 레일리 산란 더 또렷, Mie가 웜하게 영향
		atmosCB->CameraPosPlanetCoord = XMFLOAT3(0.0f, 6360002.0f, 0.0f);
		atmosCB->SunDir				= XMFLOAT3(0.0f, -0.5736f, -0.8192f);
		atmosCB->SunExposure		= 12.0f;
		atmosCB->SunIrradiance		= XMFLOAT3(1.45f, 1.0f, 0.95f);
		atmosCB->MieG				= 0.82f;
		atmosCB->MieTint			= XMFLOAT3(0.93f, 0.93f, 0.97f);
		break;
	case AtmosPreset_WinterSky:
		// 4) 청명한 겨울 하늘 (차갑고 선명)
		//	태양 고도: 25°, 방위: -Z
		//	인상: 푸른 채도↑, 맑고 차가운 느낌
		atmosCB->CameraPosPlanetCoord = XMFLOAT3(0.0f, 6360002.0f, 0.0f);
		atmosCB->SunDir				= XMFLOAT3(0.0f, -0.5736f, -0.8192f);
		atmosCB->SunExposure		= 12.0f;
		atmosCB->SunIrradiance		= XMFLOAT3(1.05f, 1.0f, 0.95f);
		atmosCB->MieG				= 0.82f;
		atmosCB->MieTint			= XMFLOAT3(0.93f, 0.93f, 0.97f);
		break;
	case AtmosPreset_HazyDay:
		// 5) 연무 / 안개 낀 날(헤이즈 강함, 콘트라스트↓)
		//	태양 고도 : 30°, 방위 : -Z
		//	인상 : 뿌연 안개, 하이라이트 확산
		atmosCB->CameraPosPlanetCoord = XMFLOAT3(0.0f, 6360002.0f, 0.0f);
		atmosCB->SunDir				= XMFLOAT3(0.0f, -0.5f, -0.8660f);
		atmosCB->SunExposure		= 18.0f;                           // 노출↑
		atmosCB->SunIrradiance		= XMFLOAT3(1.0f, 1.0f, 0.95f);
		atmosCB->MieG				= 0.90f;                           // 전방산란 강하게
		atmosCB->MieTint			= XMFLOAT3(0.95f, 0.95f, 0.97f);   // 약간 회백
		break;
	case AtmosPreset_BlueHour:
		// 6) 해뜨기 직전 블루아워(딥블루 / 보랏빛)
		//	태양 고도 : -3°(지평선 아래), 방위 : -Z
		//	인상 : 전반적으로 어둡고 푸른 노출, 레일리만 남는 느낌
		atmosCB->CameraPosPlanetCoord = XMFLOAT3(0.0f, 6360002.0f, 0.0f);
		atmosCB->SunDir				= XMFLOAT3(0.0f, +0.05234f, -0.99863f); // 태양이 지평선 아래면 Y가 +일 수 있음(Sun→Ground)
		atmosCB->SunExposure		= 20.0f;                           // 꽤 올려서 보정
		atmosCB->SunIrradiance		= XMFLOAT3(0.6f, 0.7f, 1.0f);      // 쿨톤
		atmosCB->MieG				= 0.82f;
		atmosCB->MieTint			= XMFLOAT3(0.90f, 0.92f, 1.00f);
		break;
	case AtmosPreset_HighAltitude:
		// 7) 고공 / 항공 시점(대기 얇게 보이고 지평선에 띠)
		//	카메라: +2km
		//	태양 고도 : 45°, 방위 : -Z
		atmosCB->CameraPosPlanetCoord = XMFLOAT3(0.0f, 6362000.0f, 0.0f);     // +2km
		atmosCB->SunDir				= XMFLOAT3(0.0f, -0.7071f, -0.7071f);
		atmosCB->SunExposure		= 12.0f;
		atmosCB->SunIrradiance		= XMFLOAT3(1.0f, 1.0f, 1.0f);
		atmosCB->MieG				= 0.82f;
		atmosCB->MieTint			= XMFLOAT3(0.92f, 0.93f, 0.98f);
		break;
	case AtmosPreset_ExtraSolar:
		// 8) 화성풍(붉은 먼지 많은 외계 행성 느낌)
		//	인상: 레일리 약화, Mie가 붉은 틴트로 강함
		// (참고 : 물리 파라미터까지 바꾸려면 CPU 베이크에 스펙트럼 조정이 필요하지만, 셰이더 상수만으로 분위기 연출 가능)
		atmosCB->CameraPosPlanetCoord = XMFLOAT3(0.0f, 3360002.0f, 0.0f);     // 임의의 작은 행성 반지름 씬
		atmosCB->SunDir				= XMFLOAT3(0.0f, -0.5f, -0.8660f);
		atmosCB->SunExposure		= 14.0f;
		atmosCB->SunIrradiance		= XMFLOAT3(2.0f, 0.95f, 0.85f);   // 살짝 웜
		atmosCB->MieG				= 0.88f;
		atmosCB->MieTint			= XMFLOAT3(1.0f, 0.82f, 0.7f);     // 붉은 먼지
		break;
	default:
		ASSERT(false, "Sky: Unknown atmosphere preset");
	}

	// Fixed parameters. Resolution must match the precomputed LUTs.
	atmosCB->PlanetRadius = 6'360'000.0f; // [m]
	atmosCB->AtmosphereHeight = 60'000.0f; // [m]
	atmosCB->TopRadius = atmosCB->PlanetRadius + atmosCB->AtmosphereHeight;
	atmosCB->TW = 256.0f;	// Transmittance W
	atmosCB->TH = 64.0f;	// Transmittance H
	atmosCB->SR = 32.0f;	// Scattering R
	atmosCB->SMU = 128.0f;	// Scattering Mu
	atmosCB->SMUS = 32.0f;	// Scattering MuS
	atmosCB->SNU = 8.0f;	// Scattering Nu

	// SRV table (t0=Transmittance, t1=Scattering, t2=Irradiance)
	CD3DX12_CPU_DESCRIPTOR_HANDLE cpuDescriptorTable = {};
	CD3DX12_GPU_DESCRIPTOR_HANDLE gpuDescriptorTable = {};
	ID3D12DescriptorHeap* pSRVDescriptorHeap = pDescriptorPool->GetDescriptorHeap();
	const UINT requiredSrvCount = 3; // Transmittance, Scattering, Irradiance
	bool bOk = pDescriptorPool->AllocDescriptorTable(&cpuDescriptorTable, &gpuDescriptorTable, requiredSrvCount);
	ASSERT(bOk, "Sky: Failed to allocate descriptor table for LUTs.");

	// Copy 3 LUT SRVs to the descriptor table
	CD3DX12_CPU_DESCRIPTOR_HANDLE cpuCurr = cpuDescriptorTable;
	pDevice->CopyDescriptorsSimple(1, cpuCurr, m_pTransmittanceTex->SRV, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
	cpuCurr.Offset(1, srvDescriptorSize);
	pDevice->CopyDescriptorsSimple(1, cpuCurr, m_pScatteringTex->SRV, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
	cpuCurr.Offset(1, srvDescriptorSize);
	pDevice->CopyDescriptorsSimple(1, cpuCurr, m_pIrradianceTex->SRV, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

	// ---- RS/PSO/IA
	pCommandList->SetGraphicsRootSignature(pRootSigatureManager->Query(ERootSignatureType::GraphicsDefault));
	pCommandList->SetPipelineState(m_pPSOHandle->pPSO);
	pCommandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);

	// ---- CB binding (b0, b1)
	pCommandList->SetDescriptorHeaps(1, &pSRVDescriptorHeap);
	pCommandList->SetGraphicsRootConstantBufferView(/*slot b0*/0, cb0->pGPUMemAddr);
	pCommandList->SetGraphicsRootConstantBufferView(/*slot b1*/1, cb1->pGPUMemAddr);

	// --- SRV (t0, t1, t2)
	pCommandList->SetGraphicsRootDescriptorTable(2, gpuDescriptorTable);

	pCommandList->DrawInstanced(4, 1, 0, 0);
}

bool SkyObject::initPipelineState()
{
	HRESULT hr = S_OK;

	ID3D12Device5* pDevice = m_pRenderer->GetD3DDevice();
	ShaderManager* pShaderManager = m_pRenderer->GetShaderManager();
	RootSignatureManager* pRootSigManager = m_pRenderer->GetRootSignatureManager();
	PSOManager* pPSOManager = m_pRenderer->GetPSOManager();

	ShaderHandle* pVertexShader = pShaderManager->CreateShaderDXC(L"AtmosphericSky.hlsl", L"VSMain", L"vs_6_0", 0);
	ASSERT(pVertexShader, "Sky: VS compile failed");
	ShaderHandle* pPixelShader = pShaderManager->CreateShaderDXC(L"AtmosphericSky.hlsl", L"PSMain", L"ps_6_0", 0);
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