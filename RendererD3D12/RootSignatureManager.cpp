#include "pch.h"
#include "D3D12Renderer.h"
#include "RootSignatureManager.h"


bool RootSignatureManager::Initialize(D3D12Renderer* pRenderer)
{
	ASSERT(!m_pRenderer, "RootSignatureManager::Initialize called more than once.");
	m_pRenderer = pRenderer;

	constexpr size_t NUM_ROOT_SIG = static_cast<size_t>(ERootSignatureType::Count);
	m_RootSignatures.resize(NUM_ROOT_SIG);

	// Graphics Default
	{
		HRESULT hr = S_OK;
		ID3D12Device5* pDevice = m_pRenderer->GetD3DDevice();

		// b0		: per-frame CBV
		// b1		: per-draw CBV
		// t0..t63	: SRV table
		// s0..s3	: Static sampler
		D3D12_ROOT_PARAMETER params[3] = {};

		// per-frame CBV
		params[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
		params[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
		params[0].Descriptor = { /*ShaderRegister=*/0, /*Space=*/0 };

		// per-draw CBV
		params[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
		params[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
		params[1].Descriptor = { /*ShaderRegister=*/1, /*Space=*/0 };

		// SRV table
		D3D12_DESCRIPTOR_RANGE srvRange = {};
		srvRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
		srvRange.NumDescriptors = 64; // t0..t63
		srvRange.BaseShaderRegister = 0;
		srvRange.RegisterSpace = 0;
		srvRange.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;
		D3D12_ROOT_DESCRIPTOR_TABLE srvTable = { 1, &srvRange };

		params[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
		params[2].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
		params[2].DescriptorTable = srvTable;

		// Static samplers
		D3D12_STATIC_SAMPLER_DESC samplers[4] = {};
		D3DUtil::SetSamplerDesc_Wrap(&samplers[0], 0);
		D3DUtil::SetSamplerDesc_Clamp(&samplers[1], 1);
		D3DUtil::SetSamplerDesc_Border(&samplers[2], 2);
		D3DUtil::SetSamplerDesc_Mirror(&samplers[3], 3);

		D3D12_ROOT_SIGNATURE_DESC rootSignatureDesc = {};
		rootSignatureDesc.NumParameters = _countof(params);
		rootSignatureDesc.pParameters = params;
		rootSignatureDesc.NumStaticSamplers = _countof(samplers);
		rootSignatureDesc.pStaticSamplers = samplers;
		rootSignatureDesc.Flags =
			D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT |
			D3D12_ROOT_SIGNATURE_FLAG_DENY_HULL_SHADER_ROOT_ACCESS |
			D3D12_ROOT_SIGNATURE_FLAG_DENY_DOMAIN_SHADER_ROOT_ACCESS |
			D3D12_ROOT_SIGNATURE_FLAG_DENY_GEOMETRY_SHADER_ROOT_ACCESS;

		ID3DBlob* pSignature = nullptr;
		ID3DBlob* pError = nullptr;

		hr = D3D12SerializeRootSignature(&rootSignatureDesc, D3D_ROOT_SIGNATURE_VERSION_1, &pSignature, &pError);
		ASSERT(SUCCEEDED(hr), "Failed to serialize root signature.");

		ID3D12RootSignature* gfxDefaultRS;
		hr = pDevice->CreateRootSignature(0, pSignature->GetBufferPointer(), pSignature->GetBufferSize(), IID_PPV_ARGS(&gfxDefaultRS));
		ASSERT(SUCCEEDED(hr), "Failed to create root signature.");

		gfxDefaultRS->SetName(L"RS_Gfx_Default");
		m_RootSignatures[static_cast<size_t>(ERootSignatureType::GraphicsDefault)] = gfxDefaultRS;

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
	}
	// Graphics Shadow
	{

	}
	// Compute
	{

	}

	return true;
}

void RootSignatureManager::Cleanup()
{
	for (ID3D12RootSignature* pRS : m_RootSignatures)
	{
		if (pRS)
		{
			pRS->Release();
		}
	}
	m_RootSignatures.clear();
}

ID3D12RootSignature* RootSignatureManager::Query(ERootSignatureType type)
{
	ASSERT(type < ERootSignatureType::Count, "Invalid root signature type.");
	return m_RootSignatures[static_cast<size_t>(type)];
}

void RootSignatureManager::Release(ERootSignatureType type)
{
	// Do nothing for now.
}

