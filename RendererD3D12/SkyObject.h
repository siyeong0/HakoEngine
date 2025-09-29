#pragma once
#include "ConstantBuffer.h"

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
	bool initRootSinagture();
	bool initPipelineState();

private:
	D3D12Renderer* m_pRenderer = nullptr;

	ID3D12RootSignature* m_pRootSignature = nullptr;
	ID3D12PipelineState* m_pPipelineState = nullptr;

	ID3D12Resource* m_pVertexBuffer = nullptr;
	D3D12_VERTEX_BUFFER_VIEW m_VertexBufferView = {};

	ID3D12Resource* m_pIndexBuffer = nullptr;
	D3D12_INDEX_BUFFER_VIEW m_IndexBufferView = {};
};