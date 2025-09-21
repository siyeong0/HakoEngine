#include "pch.h"

// DirectXTex
#include "DirectXTex/DirectXTex/dds.h"
#include "DirectXTex/DirectXTex/DirectXTex.h"
#include "DDSTextureLoader12.h"

#include "D3D12ResourceManager.h"

using namespace DirectX;

bool D3D12ResourceManager::Initialize(ID3D12Device5* pD3DDevice)
{
	HRESULT hr = S_OK;

	m_pD3DDevice = pD3DDevice;

	D3D12_COMMAND_QUEUE_DESC queueDesc = {};
	queueDesc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
	queueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;

	if (FAILED(m_pD3DDevice->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&m_pCommandQueue))))
	{
		ASSERT(false, "Failed to CreateCommandQueue.");
		return false;
	}

	createCommandList();
	
	// Create synchronization objects.
	createFence();

	return true;
}

HRESULT D3D12ResourceManager::CreateVertexBuffer(UINT sizePerVertex, UINT numVertices, D3D12_VERTEX_BUFFER_VIEW* pOutVertexBufferView, ID3D12Resource** ppOutBuffer, void* pInitData, bool bUseGpuUploadHeaps)
{
	HRESULT hr = S_OK;

	D3D12_VERTEX_BUFFER_VIEW	VertexBufferView = {};
	ID3D12Resource*	pVertexBuffer = nullptr;
	ID3D12Resource*	pUploadBuffer = nullptr;
	UINT		VertexBufferSize = sizePerVertex * numVertices;

	D3D12_HEAP_PROPERTIES HeapProp = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT);
	if (bUseGpuUploadHeaps)
	{
		HeapProp = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_GPU_UPLOAD);
	}
	// create vertexbuffer for rendering
	hr = m_pD3DDevice->CreateCommittedResource(
		&HeapProp,
		D3D12_HEAP_FLAG_NONE,
		&CD3DX12_RESOURCE_DESC::Buffer(VertexBufferSize),
		D3D12_RESOURCE_STATE_COMMON,
		nullptr,
		IID_PPV_ARGS(&pVertexBuffer));

	if (FAILED(hr))
	{
		ASSERT(false, "Failed to CreateCommittedResource.");
		goto lb_return;
	}
	if (pInitData)
	{
		if (bUseGpuUploadHeaps)
		{
			// Copy the triangle data to the vertex buffer(VertexBuffer on GPU Memory).
			UINT8* pVertexDataBegin = nullptr;
			CD3DX12_RANGE readRange(0, 0);        // We do not intend to read from this resource on the CPU.

			hr = pVertexBuffer->Map(0, &readRange, reinterpret_cast<void**>(&pVertexDataBegin));
			memcpy(pVertexDataBegin, pInitData, VertexBufferSize);
			pVertexBuffer->Unmap(0, nullptr);
		}
		else
		{
			hr = m_pCommandAllocator->Reset();
			ASSERT(SUCCEEDED(hr), "Failed to Reset CommandAllocator.");

			hr = m_pCommandList->Reset(m_pCommandAllocator, nullptr);
			ASSERT(SUCCEEDED(hr), "Failed to Reset CommandList.");

			hr = m_pD3DDevice->CreateCommittedResource(
				&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD),
				D3D12_HEAP_FLAG_NONE,
				&CD3DX12_RESOURCE_DESC::Buffer(VertexBufferSize),
				D3D12_RESOURCE_STATE_COMMON,
				nullptr,
				IID_PPV_ARGS(&pUploadBuffer));

			if (FAILED(hr))
			{
				ASSERT(false, "Failed to CreateCommittedResource.");
				goto lb_return;
			}	
		
			// Copy the triangle data to the vertex buffer(UploadBuffer on System Memory).
			UINT8* pVertexDataBegin = nullptr;
			CD3DX12_RANGE readRange(0, 0);        // We do not intend to read from this resource on the CPU.

			hr = pUploadBuffer->Map(0, &readRange, reinterpret_cast<void**>(&pVertexDataBegin));
			if (FAILED(hr))
			{
				ASSERT(false, "Failed to Map.");
				goto lb_return;
			}
			memcpy(pVertexDataBegin, pInitData, VertexBufferSize);
			pUploadBuffer->Unmap(0, nullptr);

			m_pCommandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(pVertexBuffer, D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_COPY_DEST));
			m_pCommandList->CopyBufferRegion(pVertexBuffer, 0, pUploadBuffer, 0, VertexBufferSize);
			m_pCommandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(pVertexBuffer, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER));

			m_pCommandList->Close();

			ID3D12CommandList* ppCommandLists[] = { m_pCommandList };
			m_pCommandQueue->ExecuteCommandLists(_countof(ppCommandLists), ppCommandLists);

			fence();
			waitForFenceValue();
		}
	}
	

	// Initialize the vertex buffer view.
	VertexBufferView.BufferLocation = pVertexBuffer->GetGPUVirtualAddress();
	VertexBufferView.StrideInBytes = sizePerVertex;
	VertexBufferView.SizeInBytes = VertexBufferSize;

	*pOutVertexBufferView = VertexBufferView;
	*ppOutBuffer = pVertexBuffer;

lb_return:
	if (pUploadBuffer)
	{
		pUploadBuffer->Release();
		pUploadBuffer = nullptr;
	}
	return hr;
}

HRESULT D3D12ResourceManager::CreateIndexBuffer(UINT numIndices, D3D12_INDEX_BUFFER_VIEW* pOutIndexBufferView, ID3D12Resource **ppOutBuffer, void* pInitData, bool bUseGpuUploadHeaps)
{
	HRESULT hr = S_OK;

	D3D12_INDEX_BUFFER_VIEW	indexBufferView = {};
	ID3D12Resource*	pIndexBuffer = nullptr;
	ID3D12Resource*	pUploadBuffer = nullptr;
	UINT indexBufferSize = sizeof(WORD) * numIndices;
	
	D3D12_HEAP_PROPERTIES heapProp = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT);
	if (bUseGpuUploadHeaps)
	{
		heapProp = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_GPU_UPLOAD);
	}
	// create vertexbuffer for rendering
	hr = m_pD3DDevice->CreateCommittedResource(
		&heapProp,
		D3D12_HEAP_FLAG_NONE,
		&CD3DX12_RESOURCE_DESC::Buffer(indexBufferSize),
		D3D12_RESOURCE_STATE_COMMON,
		nullptr,
		IID_PPV_ARGS(&pIndexBuffer));

	if (FAILED(hr))
	{
		ASSERT(false, "Failed to CreateCommittedResource.");
		goto lb_return;
	}
	if (pInitData)
	{
		if (bUseGpuUploadHeaps)
		{
			// Copy the index data to the index buffer(IndexBuffer on GPU Memory).
			UINT8* pIndexDataBegin = nullptr;
			CD3DX12_RANGE readRange(0, 0);

			hr = pIndexBuffer->Map(0, &readRange, reinterpret_cast<void**>(&pIndexDataBegin));
			if (FAILED(hr))
			{
				ASSERT(false, "Failed to Map.");
				goto lb_return;
			}
			memcpy(pIndexDataBegin, pInitData, indexBufferSize);
			pIndexBuffer->Unmap(0, nullptr);
		}
		else
		{
			hr = m_pCommandAllocator->Reset();
			ASSERT(SUCCEEDED(hr), "Failed to Reset CommandAllocator.");

			hr = m_pCommandList->Reset(m_pCommandAllocator, nullptr);
			ASSERT(SUCCEEDED(hr), "Failed to Reset CommandList.");

			hr = m_pD3DDevice->CreateCommittedResource(
				&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD),
				D3D12_HEAP_FLAG_NONE,
				&CD3DX12_RESOURCE_DESC::Buffer(indexBufferSize),
				D3D12_RESOURCE_STATE_COMMON,
				nullptr,
				IID_PPV_ARGS(&pUploadBuffer));

			if (FAILED(hr))
			{
				ASSERT(false, "Failed to CreateCommittedResource.");
				goto lb_return;
			}

			// Copy the index data to the index buffer(UploadBuffer on System Memory).
			UINT8* pIndexDataBegin = nullptr;
			CD3DX12_RANGE readRange(0, 0);        // We do not intend to read from this resource on the CPU.

			hr = pUploadBuffer->Map(0, &readRange, reinterpret_cast<void**>(&pIndexDataBegin));
			if (FAILED(hr))
			{
				ASSERT(false, "Failed to Map.");
				goto lb_return;
			}
			memcpy(pIndexDataBegin, pInitData, indexBufferSize);
			pUploadBuffer->Unmap(0, nullptr);

			m_pCommandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(pIndexBuffer, D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_COPY_DEST));
			m_pCommandList->CopyBufferRegion(pIndexBuffer, 0, pUploadBuffer, 0, indexBufferSize);
			m_pCommandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(pIndexBuffer, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_INDEX_BUFFER));

			m_pCommandList->Close();

			ID3D12CommandList* ppCommandLists[] = { m_pCommandList };
			m_pCommandQueue->ExecuteCommandLists(_countof(ppCommandLists), ppCommandLists);

			fence();
			waitForFenceValue();
		}
	}
	

	// Initialize the vertex buffer view.
	indexBufferView.BufferLocation = pIndexBuffer->GetGPUVirtualAddress();
	indexBufferView.Format = DXGI_FORMAT_R16_UINT;
	indexBufferView.SizeInBytes = indexBufferSize;

	*pOutIndexBufferView = indexBufferView;
	*ppOutBuffer = pIndexBuffer;

lb_return:
	if (pUploadBuffer)
	{
		pUploadBuffer->Release();
		pUploadBuffer = nullptr;
	}
	return hr;
}

bool D3D12ResourceManager::CreateTexture(ID3D12Resource** ppOutResource, UINT width, UINT height, DXGI_FORMAT format, const uint8_t* pInitImage)
{
	HRESULT hr = S_OK;

	ID3D12Resource*	pTexResource = nullptr;
	ID3D12Resource*	pUploadBuffer = nullptr;

	D3D12_RESOURCE_DESC textureDesc = {};
	textureDesc.MipLevels = 1;
	textureDesc.Format = format;	// ex) DXGI_FORMAT_R8G8B8A8_UNORM, etc...
	textureDesc.Width = width;
	textureDesc.Height = height;
	textureDesc.Flags = D3D12_RESOURCE_FLAG_NONE;
	textureDesc.DepthOrArraySize = 1;
	textureDesc.SampleDesc.Count = 1;
	textureDesc.SampleDesc.Quality = 0;
	textureDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;

	hr = m_pD3DDevice->CreateCommittedResource(
		&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT),
		D3D12_HEAP_FLAG_NONE,
		&textureDesc,
		D3D12_RESOURCE_STATE_ALL_SHADER_RESOURCE,
		nullptr,
		IID_PPV_ARGS(&pTexResource));
	ASSERT(SUCCEEDED(hr), "Failed to CreateCommittedResource.");

	if (pInitImage)
	{
		D3D12_RESOURCE_DESC Desc = pTexResource->GetDesc();
		D3D12_PLACED_SUBRESOURCE_FOOTPRINT Footprint;
		UINT	Rows = 0;
		UINT64	RowSize = 0;
		UINT64	TotalBytes = 0;

		m_pD3DDevice->GetCopyableFootprints(&Desc, 0, 1, 0, &Footprint, &Rows, &RowSize, &TotalBytes);

		BYTE*	pMappedPtr = nullptr;
		CD3DX12_RANGE readRange(0, 0);

		UINT64 uploadBufferSize = GetRequiredIntermediateSize(pTexResource, 0, 1);

		hr = m_pD3DDevice->CreateCommittedResource(
			&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD),
			D3D12_HEAP_FLAG_NONE,
			&CD3DX12_RESOURCE_DESC::Buffer(uploadBufferSize),
			D3D12_RESOURCE_STATE_COMMON,
			nullptr,
			IID_PPV_ARGS(&pUploadBuffer));
		ASSERT(SUCCEEDED(hr), "Failed to CreateCommittedResource.");
		
		HRESULT hr = pUploadBuffer->Map(0, &readRange, reinterpret_cast<void**>(&pMappedPtr));
		ASSERT(SUCCEEDED(hr), "Failed to Map.");

		const BYTE* pSrc = pInitImage;
		BYTE* pDest = pMappedPtr;
		for (UINT y = 0; y < height; y++)
		{
			memcpy(pDest, pSrc, width * 4);
			pSrc += (width * 4);
			pDest += Footprint.Footprint.RowPitch;			
		}
		// Unmap
		pUploadBuffer->Unmap(0, nullptr);

		UpdateTextureForWrite(pTexResource, pUploadBuffer);

		pUploadBuffer->Release();
		pUploadBuffer = nullptr;
		
	}
	*ppOutResource = pTexResource;

	return true;
}

bool D3D12ResourceManager::CreateTexturePair(ID3D12Resource** ppOutResource, ID3D12Resource** ppOutUploadBuffer, UINT Width, UINT Height, DXGI_FORMAT format)
{
	HRESULT hr = S_OK;
	ID3D12Resource*	pTexResource = nullptr;
	ID3D12Resource*	pUploadBuffer = nullptr;

	D3D12_RESOURCE_DESC textureDesc = {};
	textureDesc.MipLevels = 1;
	textureDesc.Format = format;	// ex) DXGI_FORMAT_R8G8B8A8_UNORM, etc...
	textureDesc.Width = Width;
	textureDesc.Height = Height;
	textureDesc.Flags = D3D12_RESOURCE_FLAG_NONE;
	textureDesc.DepthOrArraySize = 1;
	textureDesc.SampleDesc.Count = 1;
	textureDesc.SampleDesc.Quality = 0;
	textureDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;

	hr = m_pD3DDevice->CreateCommittedResource(
		&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT),
		D3D12_HEAP_FLAG_NONE,
		&textureDesc,
		D3D12_RESOURCE_STATE_ALL_SHADER_RESOURCE,
		nullptr,
		IID_PPV_ARGS(&pTexResource));
	ASSERT(SUCCEEDED(hr), "Failed to CreateCommittedResource.");

	uint64_t uploadBufferSize = GetRequiredIntermediateSize(pTexResource, 0, 1);

	hr = m_pD3DDevice->CreateCommittedResource(
		&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD),
		D3D12_HEAP_FLAG_NONE,
		&CD3DX12_RESOURCE_DESC::Buffer(uploadBufferSize),
		D3D12_RESOURCE_STATE_COMMON,
		nullptr,
		IID_PPV_ARGS(&pUploadBuffer));
	ASSERT(SUCCEEDED(hr), "Failed to CreateCommittedResource.");
	
	*ppOutResource = pTexResource;
	*ppOutUploadBuffer = pUploadBuffer;

	return true;
}

bool D3D12ResourceManager::CreateTextureFromFile(ID3D12Resource** ppOutResource, D3D12_RESOURCE_DESC* pOutDesc, const WCHAR* wchFileName, bool bUseGpuUploadHeaps)
{
	HRESULT hr = S_OK;

	ID3D12Resource*	pTexResource = nullptr;
	ID3D12Resource*	pUploadBuffer = nullptr;

	D3D12_HEAP_PROPERTIES heapProp = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT);
	if (bUseGpuUploadHeaps)
	{
		heapProp = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_GPU_UPLOAD);
	}
	D3D12_RESOURCE_DESC textureDesc = {};

	std::unique_ptr<uint8_t[]> ddsData;
	std::vector<D3D12_SUBRESOURCE_DATA> subresouceData;
	if (FAILED(LoadDDSTextureFromFile(m_pD3DDevice, wchFileName, &pTexResource, &heapProp, ddsData, subresouceData)))
	{
		return false;
	}
	textureDesc = pTexResource->GetDesc();
	UINT subresoucesize = (UINT)subresouceData.size();	
	UINT64 uploadBufferSize = GetRequiredIntermediateSize(pTexResource, 0, subresoucesize);

	if (bUseGpuUploadHeaps)
	{
		for (UINT i = 0; i < subresoucesize; i++)
		{
			pTexResource->WriteToSubresource(i, nullptr, subresouceData[i].pData, 
				static_cast<UINT>(subresouceData[i].RowPitch), static_cast<UINT>(subresouceData[i].SlicePitch));		}
	}
	else
	{
		// Create the GPU upload buffer.
		hr = m_pD3DDevice->CreateCommittedResource(
			&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD),
			D3D12_HEAP_FLAG_NONE,
			&CD3DX12_RESOURCE_DESC::Buffer(uploadBufferSize),
			D3D12_RESOURCE_STATE_COMMON,
			nullptr,
			IID_PPV_ARGS(&pUploadBuffer));
		ASSERT(SUCCEEDED(hr), "Failed to CreateCommittedResource.");

		hr = m_pCommandAllocator->Reset();
		ASSERT(SUCCEEDED(hr), "Failed to Reset CommandAllocator.");	

		hr = m_pCommandList->Reset(m_pCommandAllocator, nullptr);
		ASSERT(SUCCEEDED(hr), "Failed to Reset CommandList.");

		m_pCommandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(pTexResource, D3D12_RESOURCE_STATE_ALL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_COPY_DEST));
		UpdateSubresources(m_pCommandList, pTexResource, pUploadBuffer, 0, 0, subresoucesize, &subresouceData[0]);
		m_pCommandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(pTexResource, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_ALL_SHADER_RESOURCE));

		m_pCommandList->Close();

		ID3D12CommandList* ppCommandLists[] = { m_pCommandList };
		m_pCommandQueue->ExecuteCommandLists(_countof(ppCommandLists), ppCommandLists);

		fence();
		waitForFenceValue();

		if (pUploadBuffer)
		{
			pUploadBuffer->Release();
			pUploadBuffer = nullptr;
		}
	}

	*ppOutResource = pTexResource;
	*pOutDesc = textureDesc;

	return true;
}

void D3D12ResourceManager::UpdateTextureForWrite(ID3D12Resource* pDestTexResource, ID3D12Resource* pSrcTexResource)
{
	HRESULT hr = S_OK;

	constexpr UINT MAX_SUB_RESOURCE_NUM = 32;
	D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint[MAX_SUB_RESOURCE_NUM] = {};
	UINT rows[MAX_SUB_RESOURCE_NUM] = {};
	UINT64 rowSizes[MAX_SUB_RESOURCE_NUM] = {};
	UINT64 totalBytes = 0;

	D3D12_RESOURCE_DESC Desc = pDestTexResource->GetDesc();
	ASSERT(Desc.MipLevels <= (UINT)_countof(footprint));

	m_pD3DDevice->GetCopyableFootprints(&Desc, 0, Desc.MipLevels, 0, footprint, rows, rowSizes, &totalBytes);
	
	hr = m_pCommandAllocator->Reset();
	ASSERT(SUCCEEDED(hr), "Failed to Reset CommandAllocator.");

	hr = m_pCommandList->Reset(m_pCommandAllocator, nullptr);
	ASSERT(SUCCEEDED(hr), "Failed to Reset CommandList.");

	m_pCommandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(pDestTexResource, D3D12_RESOURCE_STATE_ALL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_COPY_DEST));
	for (UINT i = 0; i < Desc.MipLevels; i++)
	{
		D3D12_TEXTURE_COPY_LOCATION	destLocation = {};
		destLocation.PlacedFootprint = footprint[i];
		destLocation.pResource = pDestTexResource;
		destLocation.SubresourceIndex = i;
		destLocation.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;

		D3D12_TEXTURE_COPY_LOCATION	srcLocation = {};
		srcLocation.PlacedFootprint = footprint[i];
		srcLocation.pResource = pSrcTexResource;
		srcLocation.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;

		m_pCommandList->CopyTextureRegion(&destLocation, 0, 0, 0, &srcLocation, nullptr);
	}
	m_pCommandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(pDestTexResource, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_ALL_SHADER_RESOURCE));
	m_pCommandList->Close();

	ID3D12CommandList* ppCommandLists[] = { m_pCommandList };
	m_pCommandQueue->ExecuteCommandLists(_countof(ppCommandLists), ppCommandLists);

	fence();
	waitForFenceValue();
}

void D3D12ResourceManager::Cleanup()
{
	waitForFenceValue();

	if (m_pCommandQueue)
	{
		m_pCommandQueue->Release();
		m_pCommandQueue = nullptr;
	}

	cleanupCommandList();

	cleanupFence();
}

void D3D12ResourceManager::createFence()
{
	HRESULT hr = S_OK;

	// Create synchronization objects and wait until assets have been uploaded to the GPU.
	hr = m_pD3DDevice->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&m_pFence));
	ASSERT(SUCCEEDED(hr), "Failed to CreateFence.");

	m_ui64FenceValue = 0;

	// Create an event handle to use for frame synchronization.
	m_hFenceEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);
}

void D3D12ResourceManager::cleanupFence()
{
	if (m_hFenceEvent)
	{
		CloseHandle(m_hFenceEvent);
		m_hFenceEvent = nullptr;
	}
	if (m_pFence)
	{
		m_pFence->Release();
		m_pFence = nullptr;
	}
}

void D3D12ResourceManager::createCommandList()
{
	HRESULT hr = S_OK;

	hr = m_pD3DDevice->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&m_pCommandAllocator));
	ASSERT(SUCCEEDED(hr), "Failed to CreateCommandAllocator.");

	// Create the command list.
	hr = m_pD3DDevice->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, m_pCommandAllocator, nullptr, IID_PPV_ARGS(&m_pCommandList));
	ASSERT(SUCCEEDED(hr), "Failed to CreateCommandList.");

	// Command lists are created in the recording state, but there is nothing
	// to record yet. The main loop expects it to be closed, so close it now.
	m_pCommandList->Close();
}

void D3D12ResourceManager::cleanupCommandList()
{
	if (m_pCommandList)
	{
		m_pCommandList->Release();
		m_pCommandList = nullptr;
	}
	if (m_pCommandAllocator)
	{
		m_pCommandAllocator->Release();
		m_pCommandAllocator = nullptr;
	}
}

UINT64 D3D12ResourceManager::fence()
{
	m_ui64FenceValue++;
	m_pCommandQueue->Signal(m_pFence, m_ui64FenceValue);
	return m_ui64FenceValue;
}

void D3D12ResourceManager::waitForFenceValue()
{
	const UINT64 expectedFenceValue = m_ui64FenceValue;

	// Wait until the previous frame is finished.
	if (m_pFence->GetCompletedValue() < expectedFenceValue)
	{
		m_pFence->SetEventOnCompletion(expectedFenceValue, m_hFenceEvent);
		WaitForSingleObject(m_hFenceEvent, INFINITE);
	}
}

