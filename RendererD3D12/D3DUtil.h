#pragma once

namespace D3DUtil
{
	void GetHardwareAdapter(IDXGIFactory2* pFactory, IDXGIAdapter1** ppAdapter);
	void GetSoftwareAdapter(IDXGIFactory2* pFactory, IDXGIAdapter1** ppAdapter);
	bool CheckSupportGpuUploadHeap(ID3D12Device* pD3DDevice);
	void SetDebugLayerInfo(ID3D12Device* pD3DDevice);
	void SetDefaultSamplerDesc(D3D12_STATIC_SAMPLER_DESC* pOutSamperDesc, UINT registerIndex);
	void SetSamplerDesc_Wrap(D3D12_STATIC_SAMPLER_DESC* pOutSamperDesc, UINT RegisterIndex);
	void SetSamplerDesc_Clamp(D3D12_STATIC_SAMPLER_DESC* pOutSamperDesc, UINT RegisterIndex);
	void SetSamplerDesc_Border(D3D12_STATIC_SAMPLER_DESC* pOutSamperDesc, UINT RegisterIndex);
	void SetSamplerDesc_Mirror(D3D12_STATIC_SAMPLER_DESC* pOutSamperDesc, UINT RegisterIndex);
	void SerializeAndCreateRaytracingRootSignature(ID3D12Device* pDevice, D3D12_ROOT_SIGNATURE_DESC* pDesc, ID3D12RootSignature** ppOutRootSig);
	HRESULT CreateVertexBuffer(ID3D12Device* pDevice, UINT sizePerVertex, UINT numVertices, D3D12_VERTEX_BUFFER_VIEW* pOutVertexBufferView, ID3D12Resource** ppOutBuffer);
	HRESULT CreateUploadBuffer(ID3D12Device* pDevice, void* pData, UINT64 DataSize, ID3D12Resource** ppResource, const WCHAR* wchResourceName);
	HRESULT CreateUAVBuffer(ID3D12Device* pDevice, UINT64 BufferSize, ID3D12Resource** ppResource, D3D12_RESOURCE_STATES initialResourceState, const WCHAR* wchResourceName);
	void UpdateTexture(ID3D12Device* pD3DDevice, ID3D12GraphicsCommandList6* pCommandList, ID3D12Resource* pDestTexResource, ID3D12Resource* pSrcTexResource);

	inline size_t AlignConstantBufferSize(size_t size)
	{
		size_t alignedSize = (size + 255) & (~255);
		return alignedSize;
	}

	inline UINT Align(UINT size, UINT alignment)
	{
		return (size + (alignment - 1)) & ~(alignment - 1);
	}
} // namespace D3DUtil
#define SizeOfInUint32(obj) ((sizeof(obj) - 1) / sizeof(UINT32) + 1)