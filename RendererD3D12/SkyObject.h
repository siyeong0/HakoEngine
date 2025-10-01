#pragma once
#include "ConstantBuffer.h"

struct PSOHandle;
class D3D12Renderer;

class SkyObject
{
public:
	bool Initialize(D3D12Renderer* pRenderer);
	void Draw(int threadIndex, ID3D12GraphicsCommandList6* pCommandList);
	void Cleanup();

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

	TextureHandle* m_pTransmittanceTex = nullptr;
	TextureHandle* m_pScatteringTex = nullptr;
	TextureHandle* m_pIrradianceTex = nullptr;
};