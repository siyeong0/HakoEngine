#pragma once
struct PSOHandle;
class D3D12Renderer;

enum class EAtmosPreset
{
	NoonClearSky,
	AfternoonClearSky,
	GoldenHour,
	WinterSky,
	HazyDay,
	BlueHour,
	HighAltitude,
	ExtraSolar
};

class SkyObject
{
public:
	bool Initialize(D3D12Renderer* pRenderer);
	void Cleanup();
	void Draw(int threadIndex, ID3D12GraphicsCommandList6* pCommandList);

	D3D12Texture* GetTransmittanceTexture() const { return m_pTransmittanceTex; }
	D3D12Texture* GetScatteringTexture() const { return m_pScatteringTex; }
	D3D12Texture* GetIrradianceTexture() const { return m_pIrradianceTex; }

	const CONSTANT_BUFFER_ATMOS& GetCBData() const { return m_AtmosCBData; }
	static void SetAtmosStateFromPreset(CONSTANT_BUFFER_ATMOS* outDst, EAtmosPreset ePreset);

	void SetSunDir(const FLOAT3& dir) { m_AtmosCBData.SunDir = dir; }

	SkyObject() = default;
	~SkyObject() { Cleanup(); };

private:
	bool initPipelineState();

private:
	D3D12Renderer* m_pRenderer = nullptr;

	ID3D12Resource* m_pVertexBuffer = nullptr;
	D3D12_VERTEX_BUFFER_VIEW m_VertexBufferView = {};
	ID3D12Resource* m_pIndexBuffer = nullptr;
	D3D12_INDEX_BUFFER_VIEW m_IndexBufferView = {};
	PSOHandle* m_pPSOHandle = nullptr;

	CONSTANT_BUFFER_ATMOS m_AtmosCBData = {};

	D3D12Texture* m_pTransmittanceTex = nullptr;
	D3D12Texture* m_pScatteringTex = nullptr;
	D3D12Texture* m_pIrradianceTex = nullptr;
};