#pragma once

struct PSOHandle
{
	std::string Name;
	std::size_t Hash = 0;
	ID3D12PipelineState* pPSO = nullptr;
	int RefCount = 0;
};

class D3D12Renderer;

class PSOManager
{
public:
	PSOManager() = default;
	~PSOManager() { Cleanup(); }

	bool Initialize(D3D12Renderer* pRenderer);
	void Cleanup();

	PSOHandle* CreatePSO(const D3D12_GRAPHICS_PIPELINE_STATE_DESC& posDesc, const std::string& psoName = "");
	PSOHandle* QueryPSO(PSOHandle* handle);
	void ReleasePSO(PSOHandle* handle);

private:
	std::size_t hashFunc(const D3D12_GRAPHICS_PIPELINE_STATE_DESC& d) const noexcept;

private:
	D3D12Renderer* m_pRenderer = nullptr;
	ID3D12Device* m_pDevice = nullptr;
	std::unordered_map<std::size_t, PSOHandle> m_PSOMap;
	std::mutex m_Mutex;
};