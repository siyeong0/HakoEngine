#pragma once

enum class ERootSignatureType : uint32_t
{
	GraphicsDefault = 0,
	GraphicsShadow,
	Compute,
	Count
};

class D3D12Renderer;

class RootSignatureManager
{
public:
	RootSignatureManager() = default;
	~RootSignatureManager() { Cleanup(); }

	bool Initialize(D3D12Renderer* pRenderer);
	void Cleanup();

	ID3D12RootSignature* Query(ERootSignatureType type);
	void Release(ERootSignatureType type);

private:

private:
	D3D12Renderer* m_pRenderer = nullptr;
	std::vector<ID3D12RootSignature*> m_RootSignatures;
};