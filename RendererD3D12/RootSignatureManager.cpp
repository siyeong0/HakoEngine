#include "pch.h"
#include "D3D12Renderer.h"
#include "RootSignatureManager.h"


bool RootSignatureManager::Initialize(D3D12Renderer* pRenderer)
{
	ASSERT(!m_pRenderer, "RootSignatureManager::Initialize called more than once.");
	m_pRenderer = pRenderer;

	constexpr size_t NUM_ROOT_SIG = static_cast<size_t>(ERootSignatureType::Count);
	m_RootSignatures.resize(NUM_ROOT_SIG);

	ID3D12Device5* pDevice = m_pRenderer->GetD3DDevice();
	// Graphics Default
	{
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

		D3DUtil::SerializeAndCreateRaytracingRootSignature(
			pDevice, 
			&rootSignatureDesc, 
			&m_RootSignatures[static_cast<size_t>(ERootSignatureType::GraphicsDefault)]);
	}
	// Graphics Raytracing Global
	{
		// This is a root signature that is shared across all raytracing shaders invoked during a DispatchRays() call.

		// root param 0
		// output-diffuse(uav) | output-depth(uav)

		// root param 1
		// Acceleration Sturecture

		CD3DX12_DESCRIPTOR_RANGE globalRanges[4] = {};
		globalRanges[0].Init(D3D12_DESCRIPTOR_RANGE_TYPE_CBV, 1, /*b*/0, /*space*/0); // b0, space0
		globalRanges[1].Init(D3D12_DESCRIPTOR_RANGE_TYPE_CBV, 1, /*b*/0, /*space*/1); // b0, space1
		globalRanges[2].Init(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 2, /*u*/0, /*space*/0); // u0 : u0-diffuse | u1 : out-depth
		globalRanges[3].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 3, /*t*/11, /*space*/0); // t0 : AccelerationStructure

		// b0 : RaytracingCBV | u0 : u0-diffuse | u1 : out-depth | t0 : AccelerationStructure
		CD3DX12_ROOT_PARAMETER globalRootParameters[2] = {};
		globalRootParameters[0].InitAsDescriptorTable(_countof(globalRanges), globalRanges, D3D12_SHADER_VISIBILITY_ALL);
		globalRootParameters[1].InitAsShaderResourceView(0);	// Acceleration Structure

		// sampler
		D3D12_STATIC_SAMPLER_DESC samplers[4] = {};
		D3DUtil::SetSamplerDesc_Wrap(samplers + 0, 0);	// Wrap Linear
		D3DUtil::SetSamplerDesc_Clamp(samplers + 1, 1);	// Clamp Linear
		D3DUtil::SetSamplerDesc_Wrap(samplers + 2, 2);	// Wrap Point
		samplers[2].Filter = D3D12_FILTER_MIN_MAG_MIP_POINT;
		D3DUtil::SetSamplerDesc_Mirror(samplers + 3, 3);	// Mirror Linear
		samplers[3].Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
		for (uint i = 0; i < (uint)_countof(samplers); ++i)
		{
			samplers[i].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
		}
		CD3DX12_ROOT_SIGNATURE_DESC globalRootSignatureDesc(ARRAYSIZE(globalRootParameters), globalRootParameters, (DWORD)_countof(samplers), samplers);
		D3DUtil::SerializeAndCreateRaytracingRootSignature(
			pDevice,
			&globalRootSignatureDesc, 
			&m_RootSignatures[static_cast<size_t>(ERootSignatureType::GraphicsRaytracingGlobal)]);
	}
	// Graphics Raytracing Local
	{
		// Local Root Signature
		// space1
		// t0 : vertex buffer, t1 : index buffer, t2 : diffuse texture, t3 : normal texture
		CD3DX12_DESCRIPTOR_RANGE localRanges[1] = {};
		localRanges[0].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 4, 0, 1);	// space1

		CD3DX12_ROOT_PARAMETER localRootParameters[2] = {};
		localRootParameters[0].InitAsConstants(SizeOfInUint32(CONSTANT_BUFFER_RT_TRIGROUP), /*b*/1, /*space*/0, D3D12_SHADER_VISIBILITY_ALL);
		localRootParameters[1].InitAsDescriptorTable(_countof(localRanges), localRanges, D3D12_SHADER_VISIBILITY_ALL);

		CD3DX12_ROOT_SIGNATURE_DESC localRootSignatureDesc(ARRAYSIZE(localRootParameters), localRootParameters, 0, nullptr);
		localRootSignatureDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_LOCAL_ROOT_SIGNATURE;
		D3DUtil::SerializeAndCreateRaytracingRootSignature(
			pDevice,
			&localRootSignatureDesc,
			&m_RootSignatures[static_cast<size_t>(ERootSignatureType::GraphicsRaytracingLocal)]);
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

