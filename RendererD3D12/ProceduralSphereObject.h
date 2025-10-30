#pragma once

class D3D12Renderer;
struct PSOHandle;

class ProceduralSphereObject : public IProceduralSphereObject
{
public:
	// Derived from IUnknown
	STDMETHODIMP			QueryInterface(REFIID, void** ppv);
	STDMETHODIMP_(ULONG)	AddRef();
	STDMETHODIMP_(ULONG)	Release();

	bool ENGINECALL BeginCreateGeom(uint numSpheres, const UMaterial& material) override;
	bool ENGINECALL InsertSphere(const Sphere& sphereData) override;
	void ENGINECALL EndCreateGeom(bool bUseRayTracingIfSupported = true) override;

	uint ENGINECALL GetRenderPass() override;

	// Internal
	ProceduralSphereObject() = default;
	~ProceduralSphereObject() { cleanup(); }

	bool Initialize(D3D12Renderer* pRenderer);
	void Draw(int threadIndex, ID3D12GraphicsCommandList6* pCommandList, const Matrix4x4& worldMatrix);
	void UpdateBLAS(int threadIndex, ID3D12GraphicsCommandList6* pCommandList, const Matrix4x4& worldMatrix);

private:
	void cleanup();

private:
	int m_RefCount = 1;
	D3D12Renderer* m_pRenderer = nullptr;

	ID3D12Resource* m_pAABBBuffer = nullptr;
	ID3D12Resource* m_pSphereDataBuffer = nullptr;

	std::vector<Sphere> m_SphereDataArray;
	CBMaterial m_Material;
	bool m_bOpaque = true;
	uint m_MaxNumSpheres = 0;

	BLASHandle* m_pBLASHandle = nullptr;

	uint m_RenderPass = -1;
};