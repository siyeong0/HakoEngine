#pragma once

class D3D12ResourceManager
{
public:
	D3D12ResourceManager() = default;
	~D3D12ResourceManager() { Cleanup(); }

	bool Initialize(ID3D12Device5* pD3DDevice);
	HRESULT CreateVertexBuffer(size_t sizePerVertex, size_t numVertices, D3D12_VERTEX_BUFFER_VIEW* pOutVertexBufferView, ID3D12Resource **ppOutBuffer, void* pInitData, bool bUseGpuUploadHeaps);
	HRESULT CreateIndexBuffer(size_t numIndices, D3D12_INDEX_BUFFER_VIEW* pOutIndexBufferView, ID3D12Resource **ppOutBuffer, void* pInitData, bool bUseGpuUploadHeaps);
	void UpdateTextureForWrite(ID3D12Resource* pDestTexResource, ID3D12Resource* pSrcTexResource);
	bool CreateTexture(ID3D12Resource** ppOutResource, UINT width, UINT height, DXGI_FORMAT format, const uint8_t* pInitImage);
	bool CreateTextureFromFile(ID3D12Resource** ppOutResource, D3D12_RESOURCE_DESC* pOutDesc, const WCHAR* wchFileName, bool bUseGpuUploadHeaps);
	bool CreateTexturePair(ID3D12Resource** ppOutResource, ID3D12Resource** ppOutUploadBuffer, UINT width, UINT height, DXGI_FORMAT format);
	void Cleanup();

private:
	void createFence();
	void cleanupFence();

	void createCommandList();
	void cleanupCommandList();

	uint64_t fence();
	void waitForFenceValue();

private:
	ID3D12Device5* m_pD3DDevice = nullptr;
	ID3D12CommandQueue* m_pCommandQueue = nullptr;
	ID3D12CommandAllocator* m_pCommandAllocator = nullptr;
	ID3D12GraphicsCommandList6* m_pCommandList = nullptr;

	HANDLE m_hFenceEvent = nullptr;
	ID3D12Fence* m_pFence = nullptr;
	uint64_t m_ui64FenceValue = 0;
};

