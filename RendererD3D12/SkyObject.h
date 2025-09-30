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
	bool initRootSinagture();
	bool initPipelineState();

private:
	D3D12Renderer* m_pRenderer = nullptr;

	static ID3D12RootSignature* m_pRootSignature; // TODO : Use RootSignaturePool
	PSOHandle* m_pPSOHandle = nullptr;

	ID3D12Resource* m_pVertexBuffer = nullptr;
	D3D12_VERTEX_BUFFER_VIEW m_VertexBufferView = {};

	ID3D12Resource* m_pIndexBuffer = nullptr;
	D3D12_INDEX_BUFFER_VIEW m_IndexBufferView = {};
};