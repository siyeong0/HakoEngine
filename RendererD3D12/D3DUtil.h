﻿#pragma once

void GetHardwareAdapter(IDXGIFactory2* pFactory, IDXGIAdapter1** ppAdapter);
void GetSoftwareAdapter(IDXGIFactory2* pFactory, IDXGIAdapter1** ppAdapter);
bool CheckSupportGpuUploadHeap(ID3D12Device* pD3DDevice);
void SetDebugLayerInfo(ID3D12Device* pD3DDevice);
HRESULT CreateVertexBuffer(ID3D12Device* pDevice, UINT sizePerVertex, UINT numVertices, D3D12_VERTEX_BUFFER_VIEW* pOutVertexBufferView, ID3D12Resource **ppOutBuffer);
void SetDefaultSamplerDesc(D3D12_STATIC_SAMPLER_DESC* pOutSamperDesc, UINT registerIndex);

void UpdateTexture(ID3D12Device* pD3DDevice, ID3D12GraphicsCommandList* pCommandList, ID3D12Resource* pDestTexResource, ID3D12Resource* pSrcTexResource);
inline size_t AlignConstantBufferSize(size_t size)
{
	size_t alignedSize = (size + 255) & (~255);
	return alignedSize;
}