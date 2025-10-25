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

	// Static samplers
	D3D12_STATIC_SAMPLER_DESC samplers[4] = {};
	D3DUtil::SetSamplerDesc_Wrap(samplers + 0, 0);		// Wrap Linear
	D3DUtil::SetSamplerDesc_Clamp(samplers + 1, 1);		// Clamp Linear
	D3DUtil::SetSamplerDesc_Point(samplers + 2, 2);		// Wrap Point
	D3DUtil::SetSamplerDesc_Mirror(samplers + 3, 3);	// Mirror Linear

	// Graphics Default
	{
		// b0, space0 : RaytracingCBV
		// b0, space1 : SkyAtmosphereCBV
		// u0...u1, space0 : UAV table
		// t0..t63, space0 : SRV table
		// Descriptor Ranges
		CD3DX12_DESCRIPTOR_RANGE srvRange(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 64, /*t*/0, /*space*/0); // t0..t63

		CD3DX12_ROOT_PARAMETER rootParameters[4] = {};
		rootParameters[0].InitAsConstantBufferView(0, 0, D3D12_SHADER_VISIBILITY_ALL); // Frame CBV
		rootParameters[1].InitAsConstantBufferView(0, 1, D3D12_SHADER_VISIBILITY_ALL); // Sky Atmosphere CBV
		rootParameters[2].InitAsConstantBufferView(1, 0, D3D12_SHADER_VISIBILITY_ALL); // Object CBV
		rootParameters[3].InitAsDescriptorTable(1, &srvRange, D3D12_SHADER_VISIBILITY_PIXEL);

		CD3DX12_ROOT_SIGNATURE_DESC rootSignatureDesc(ARRAYSIZE(rootParameters), rootParameters, (DWORD)_countof(samplers), samplers);
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

		// b0, space0 : RaytracingCBV
		// b0, space1 : SkyAtmosphereCBV
		// u0, space0 : out-diffuse | u1, space0 : out-depth
		// t0..t63, space0 : SRV table
		// t0, space2 : AccelerationStructure

		// Descriptor Ranges
		CD3DX12_DESCRIPTOR_RANGE viewRanges[2] = {};
		viewRanges[0].Init(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 2, /*u*/0, /*space*/0); // u0 : u0-diffuse | u1 : out-depth
		viewRanges[1].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 64, /*t*/0, /*space*/0); // t0..t63

		CD3DX12_ROOT_PARAMETER globalRootParameters[4] = {};
		globalRootParameters[0].InitAsConstantBufferView(0, 0, D3D12_SHADER_VISIBILITY_ALL); // Raytracing CBV
		globalRootParameters[1].InitAsConstantBufferView(0, 1, D3D12_SHADER_VISIBILITY_ALL); // Sky Atmosphere CBV
		globalRootParameters[2].InitAsDescriptorTable(_countof(viewRanges), viewRanges, D3D12_SHADER_VISIBILITY_ALL);
		globalRootParameters[3].InitAsShaderResourceView(0, 3);	// Acceleration Structure

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
		localRanges[0].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 4, 0, /*space*/1);	// t0..t3, space1

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

