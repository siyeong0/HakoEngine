#include "pch.h"
#include <format>
#include <mutex>
#include <unordered_map>
#include "D3D12Renderer.h"
#include "PSOManager.h"
#include <iostream>

bool PSOManager::Initialize(D3D12Renderer* pRenderer)
{
	ASSERT(!m_pRenderer && !m_pDevice, "PSOManager::Initialize called more than once.");
	m_pRenderer = pRenderer;
	m_pDevice = pRenderer->GetD3DDevice();

	return true;
}

void PSOManager::Cleanup()
{
	std::scoped_lock lock(m_Mutex);

	for (auto& kv : m_PSOMap)
	{
		ASSERT(kv.second.pPSO, "PSO pointer is null during cleanup.");
		SAFE_RELEASE(kv.second.pPSO);
	}
	m_PSOMap.clear();
	m_pDevice = nullptr;
}

PSOHandle* PSOManager::CreatePSO(const D3D12_GRAPHICS_PIPELINE_STATE_DESC& posDesc, const std::string& psoName)
{
	std::scoped_lock lock(m_Mutex);
	PSOHandle* outHandle = nullptr;
	PSOHandle handle = {};

	std::size_t hash = hashFunc(posDesc);

	auto it = m_PSOMap.find(hash);
	if (it != m_PSOMap.end())
	{
		++(it->second.RefCount);  // Reference Count
		outHandle = &(it->second);
		goto lb_return;
	}

	handle.Name = psoName == "" ? std::format("Unnamed_({})", m_PSOMap.size()) : psoName;
	handle.Hash = hashFunc(posDesc);
	HRESULT hr = m_pDevice->CreateGraphicsPipelineState(&posDesc, IID_PPV_ARGS(&handle.pPSO));
	if (FAILED(hr))
	{
		ASSERT(false, "Failed to create PSO.");
		goto lb_return;
	}
	handle.RefCount = 1;

	m_PSOMap.emplace(handle.Hash, handle);
	outHandle = &m_PSOMap[handle.Hash];

lb_return:
	return outHandle;
}

void PSOManager::ReleasePSO(PSOHandle* handle)
{
	std::scoped_lock lock(m_Mutex);

	auto it = m_PSOMap.find(handle->Hash);
	ASSERT(it != m_PSOMap.end(), "PSO handle not found in map.");
	ASSERT(handle->RefCount > 0, "PSO reference count is already 0 or negative.");

	--handle->RefCount;

	if (handle->RefCount == 0)
	{
		ASSERT(handle->pPSO, "PSO pointer is null.");
		SAFE_RELEASE(handle->pPSO);
		m_PSOMap.erase(it);
	}
}

//--------------------------------------------------------------
// Hash function for D3D12_GRAPHICS_PIPELINE_STATE_DESC
// Note: This is a deep hash function considering all relevant fields.
//--------------------------------------------------------------

struct Hasher
{
	template<class T> void Combine(size_t& s, const T& v) const noexcept
	{
		s ^= std::hash<T>{}(v)+0x9e3779b97f4a7c15ull + (s << 6) + (s >> 2);
	}

	void CombineBytes(size_t& s, const void* p, size_t n) const noexcept
	{
		const uint8_t* b = static_cast<const uint8_t*>(p);
		uint64_t h = 1469598103934665603ull;
		for (size_t i = 0; i < n; ++i)
		{
			h ^= b[i]; h *= 1099511628211ull;
		}
		Combine(s, h);
	}

	void CombineShader(size_t& s, const D3D12_SHADER_BYTECODE& bc) const noexcept
	{
		Combine(s, (size_t)bc.BytecodeLength);
		if (bc.pShaderBytecode && bc.BytecodeLength)
		{
			CombineBytes(s, bc.pShaderBytecode, bc.BytecodeLength);
		}
	}
};

std::size_t PSOManager::hashFunc(const D3D12_GRAPHICS_PIPELINE_STATE_DESC& d) const noexcept
{
	// TODO: 보완.
	Hasher hasher;
	size_t seed = 0;

	hasher.Combine(seed, reinterpret_cast<uintptr_t>(d.pRootSignature));

	// Shaders
	hasher.CombineShader(seed, d.VS);
	hasher.CombineShader(seed, d.PS);
	hasher.CombineShader(seed, d.DS);
	hasher.CombineShader(seed, d.HS);
	hasher.CombineShader(seed, d.GS);

	// InputLayout
	hasher.Combine(seed, (size_t)d.InputLayout.NumElements);
	for (uint i = 0; i < d.InputLayout.NumElements; ++i)
	{
		const auto& e = d.InputLayout.pInputElementDescs[i];
		if (e.SemanticName)
		{
			hasher.CombineBytes(seed, e.SemanticName, std::strlen(e.SemanticName));
		}
		hasher.Combine(seed, (size_t)e.SemanticIndex);
		hasher.Combine(seed, (size_t)e.Format);
		hasher.Combine(seed, (size_t)e.InputSlot);
		hasher.Combine(seed, (size_t)e.AlignedByteOffset);
		hasher.Combine(seed, (size_t)e.InputSlotClass);
		hasher.Combine(seed, (size_t)e.InstanceDataStepRate);
	}

	// Rasterizer
	const auto& r = d.RasterizerState;
	hasher.Combine(seed, (size_t)r.FillMode);
	hasher.Combine(seed, (size_t)r.CullMode);
	hasher.Combine(seed, (size_t)r.FrontCounterClockwise);
	hasher.Combine(seed, (size_t)r.DepthBias);
	hasher.Combine(seed, r.DepthBiasClamp);
	hasher.Combine(seed, r.SlopeScaledDepthBias);
	hasher.Combine(seed, (size_t)r.DepthClipEnable);
	hasher.Combine(seed, (size_t)r.MultisampleEnable);
	hasher.Combine(seed, (size_t)r.AntialiasedLineEnable);
	hasher.Combine(seed, (size_t)r.ForcedSampleCount);
	hasher.Combine(seed, (size_t)r.ConservativeRaster);

	// Blend
	const auto& b = d.BlendState;
	hasher.Combine(seed, (size_t)b.AlphaToCoverageEnable);
	hasher.Combine(seed, (size_t)b.IndependentBlendEnable);
	for (int i = 0; i < 8; ++i)
	{
		const auto& rt = b.RenderTarget[i];
		hasher.Combine(seed, (size_t)rt.BlendEnable);
		hasher.Combine(seed, (size_t)rt.LogicOpEnable);
		hasher.Combine(seed, (size_t)rt.SrcBlend);
		hasher.Combine(seed, (size_t)rt.DestBlend);
		hasher.Combine(seed, (size_t)rt.BlendOp);
		hasher.Combine(seed, (size_t)rt.SrcBlendAlpha);
		hasher.Combine(seed, (size_t)rt.DestBlendAlpha);
		hasher.Combine(seed, (size_t)rt.BlendOpAlpha);
		hasher.Combine(seed, (size_t)rt.LogicOp);
		hasher.Combine(seed, (size_t)rt.RenderTargetWriteMask);
	}

	// DepthStencil
	const auto& ds = d.DepthStencilState;
	hasher.Combine(seed, (size_t)ds.DepthEnable);
	hasher.Combine(seed, (size_t)ds.DepthWriteMask);
	hasher.Combine(seed, (size_t)ds.DepthFunc);
	hasher.Combine(seed, (size_t)ds.StencilEnable);
	hasher.Combine(seed, (size_t)ds.StencilReadMask);
	hasher.Combine(seed, (size_t)ds.StencilWriteMask);
	hasher.CombineBytes(seed, &ds.FrontFace, sizeof(ds.FrontFace));
	hasher.CombineBytes(seed, &ds.BackFace, sizeof(ds.BackFace));

	// StreamOutput (사용 시 깊게 처리)
	// if (d.StreamOutput.NumEntries) { ... d.StreamOutput.pSODeclaration ... }

	// Output/Misc
	hasher.Combine(seed, (size_t)d.NumRenderTargets);
	for (uint i = 0; i < d.NumRenderTargets; ++i)
	{
		hasher.Combine(seed, (size_t)d.RTVFormats[i]);
	}
	hasher.Combine(seed, (size_t)d.DSVFormat);
	hasher.Combine(seed, (size_t)d.IBStripCutValue);
	hasher.Combine(seed, (size_t)d.PrimitiveTopologyType);
	hasher.CombineBytes(seed, &d.SampleDesc, sizeof(d.SampleDesc));
	hasher.Combine(seed, (size_t)d.SampleMask);
	hasher.Combine(seed, (size_t)d.NodeMask);
	hasher.Combine(seed, (size_t)d.Flags);

	// CachedPSO (사용 시 내용 기반으로)
	// if (d.CachedPSO.pCachedBlob && d.CachedPSO.CachedBlobSizeInBytes)
	//     H.combine_bytes(seed, d.CachedPSO.pCachedBlob, d.CachedPSO.CachedBlobSizeInBytes);

	return seed;
}