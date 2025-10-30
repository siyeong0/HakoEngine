#include "pch.h"
#include "Generic/Image.h"
#include "D3D12ResourceManager.h"

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

#include <d3dx12.h>

HRESULT D3D12ResourceManager::CreateStructuredBuffer(
	size_t elementStride,
	size_t numElements,
	ID3D12Resource** ppOutBuffer,
	void* pInitData,
	bool bUseGpuUploadHeaps)
{
	HRESULT hr = S_OK;

	// Argument validation
	ASSERT(m_pD3DDevice, "D3D device must be initialized.");
	ASSERT(ppOutBuffer, "ppOutBuffer must not be null.");
	ASSERT(elementStride > 0, "elementStride must be greater than zero.");
	ASSERT((elementStride % 4) == 0, "elementStride must be a multiple of 4.");
	ASSERT(numElements > 0, "numElements must be greater than zero.");
	ASSERT(pInitData, "pInitData must not be null.");

	*ppOutBuffer = nullptr;

	ID3D12Resource* pBuffer = nullptr; // Final resource to return
	ID3D12Resource* pUploadBuffer = nullptr; // Staging buffer when using DEFAULT heap
	const UINT64 bufferSize = static_cast<UINT64>(elementStride) * static_cast<UINT64>(numElements);

	if (bUseGpuUploadHeaps)
	{
		// UPLOAD-heap resident buffer (CPU-writable, convenient for frequent updates).
		// Note: This can be bound as SRV/UAV but is generally slower than DEFAULT.
		hr = m_pD3DDevice->CreateCommittedResource(
			&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD),
			D3D12_HEAP_FLAG_NONE,
			&CD3DX12_RESOURCE_DESC::Buffer(bufferSize),
			D3D12_RESOURCE_STATE_GENERIC_READ,   // Required state for UPLOAD heap resources
			nullptr,
			IID_PPV_ARGS(&pBuffer));

		if (FAILED(hr))
		{
			ASSERT(false, "Failed to create UPLOAD structured buffer.");
			goto lb_return;
		}

		void* mapped = nullptr;
		CD3DX12_RANGE noRead(0, 0); // CPU won't read
		hr = pBuffer->Map(0, &noRead, &mapped);
		if (FAILED(hr))
		{
			ASSERT(false, "Failed to Map() UPLOAD buffer.");
			goto lb_return;
		}
		std::memcpy(mapped, pInitData, static_cast<size_t>(bufferSize));
		pBuffer->Unmap(0, nullptr);
	}
	else
	{
		// DEFAULT-heap resident buffer (GPU local, recommended for read on GPU).
		// We'll upload via a transient UPLOAD buffer and transition to SRV-readable state.

		// Create DEFAULT buffer in COMMON state
		hr = m_pD3DDevice->CreateCommittedResource(
			&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT),
			D3D12_HEAP_FLAG_NONE,
			&CD3DX12_RESOURCE_DESC::Buffer(bufferSize),
			D3D12_RESOURCE_STATE_COMMON,
			nullptr,
			IID_PPV_ARGS(&pBuffer));

		if (FAILED(hr))
		{
			ASSERT(false, "Failed to create DEFAULT structured buffer.");
			goto lb_return;
		}

		// Create UPLOAD buffer and copy CPU data into it
		hr = m_pD3DDevice->CreateCommittedResource(
			&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD),
			D3D12_HEAP_FLAG_NONE,
			&CD3DX12_RESOURCE_DESC::Buffer(bufferSize),
			D3D12_RESOURCE_STATE_GENERIC_READ,
			nullptr,
			IID_PPV_ARGS(&pUploadBuffer));

		if (FAILED(hr))
		{
			ASSERT(false, "Failed to create UPLOAD buffer for staging.");
			goto lb_return;
		}

		void* mapped = nullptr;
		CD3DX12_RANGE noRead(0, 0); // CPU won't read
		hr = pUploadBuffer->Map(0, &noRead, &mapped);
		if (FAILED(hr))
		{
			ASSERT(false, "Failed to Map() UPLOAD staging buffer.");
			goto lb_return;
		}
		std::memcpy(mapped, pInitData, static_cast<size_t>(bufferSize));
		pUploadBuffer->Unmap(0, nullptr);

		hr = m_pCommandAllocator->Reset();
		ASSERT(SUCCEEDED(hr), "Failed to Reset CommandAllocator.");
		hr = m_pCommandList->Reset(m_pCommandAllocator, nullptr);
		ASSERT(SUCCEEDED(hr), "Failed to Reset CommandList.");

		m_pCommandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(pBuffer, D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_COPY_DEST));
		m_pCommandList->CopyBufferRegion(pBuffer, 0, pUploadBuffer, 0, bufferSize);
		m_pCommandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(pBuffer, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE));

		hr = m_pCommandList->Close();
		ASSERT(SUCCEEDED(hr), "Failed to Close CommandList.");

		ID3D12CommandList* lists[] = { m_pCommandList };
		m_pCommandQueue->ExecuteCommandLists(_countof(lists), lists);

		// GPU sync to ensure upload finishes before we destroy the staging buffer
		fence();
		waitForFenceValue();
	}

	// Output
	*ppOutBuffer = pBuffer;

lb_return:
	// Release staging buffer if any (safe to pass nullptr)
	SAFE_RELEASE(pUploadBuffer);

	// On failure, release created resource and null the out param
	if (FAILED(hr))
	{
		SAFE_RELEASE(pBuffer);
		if (ppOutBuffer) *ppOutBuffer = nullptr;
	}

	return hr;
}
HRESULT D3D12ResourceManager::CreateVertexBuffer(
	size_t sizePerVertex,
	size_t numVertices,
	D3D12_VERTEX_BUFFER_VIEW* pOutVertexBufferView,
	ID3D12Resource** ppOutBuffer,
	void* pInitData,
	bool bUseGpuUploadHeaps)
{
	HRESULT hr = S_OK;

	D3D12_VERTEX_BUFFER_VIEW vertexBufferView = {};
	ID3D12Resource* pVertexBuffer = nullptr;
	ID3D12Resource* pUploadBuffer = nullptr;
	size_t vertexBufferSize = sizePerVertex * numVertices;

	D3D12_HEAP_PROPERTIES heapProp = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT);
	if (bUseGpuUploadHeaps)
	{
		heapProp = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_GPU_UPLOAD);
	}
	// create vertexbuffer for rendering
	hr = m_pD3DDevice->CreateCommittedResource(
		&heapProp,
		D3D12_HEAP_FLAG_NONE,
		&CD3DX12_RESOURCE_DESC::Buffer(vertexBufferSize),
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
			memcpy(pVertexDataBegin, pInitData, vertexBufferSize);
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
				&CD3DX12_RESOURCE_DESC::Buffer(vertexBufferSize),
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
			memcpy(pVertexDataBegin, pInitData, vertexBufferSize);
			pUploadBuffer->Unmap(0, nullptr);

			m_pCommandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(pVertexBuffer, D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_COPY_DEST));
			m_pCommandList->CopyBufferRegion(pVertexBuffer, 0, pUploadBuffer, 0, vertexBufferSize);
			m_pCommandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(pVertexBuffer, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER));

			m_pCommandList->Close();

			ID3D12CommandList* ppCommandLists[] = { m_pCommandList };
			m_pCommandQueue->ExecuteCommandLists(_countof(ppCommandLists), ppCommandLists);

			fence();
			waitForFenceValue();
		}
	}

	// Initialize the vertex buffer view.
	vertexBufferView.BufferLocation = pVertexBuffer->GetGPUVirtualAddress();
	vertexBufferView.StrideInBytes = static_cast<uint>(sizePerVertex);
	vertexBufferView.SizeInBytes = static_cast<uint>(vertexBufferSize);

	*pOutVertexBufferView = vertexBufferView;
	*ppOutBuffer = pVertexBuffer;

lb_return:
	SAFE_RELEASE(pUploadBuffer);
	return hr;
}

HRESULT D3D12ResourceManager::CreateIndexBuffer(
	size_t numIndices,
	D3D12_INDEX_BUFFER_VIEW* pOutIndexBufferView,
	ID3D12Resource** ppOutBuffer,
	void* pInitData,
	bool bUseGpuUploadHeaps)
{
	HRESULT hr = S_OK;

	D3D12_INDEX_BUFFER_VIEW	indexBufferView = {};
	ID3D12Resource* pIndexBuffer = nullptr;
	ID3D12Resource* pUploadBuffer = nullptr;
	size_t indexBufferSize = sizeof(WORD) * numIndices;

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
	indexBufferView.SizeInBytes = static_cast<uint>(indexBufferSize);

	*pOutIndexBufferView = indexBufferView;
	*ppOutBuffer = pIndexBuffer;

lb_return:
	SAFE_RELEASE(pUploadBuffer);
	return hr;
}

bool D3D12ResourceManager::CreateTexture(
	ID3D12Resource** ppOutResource,
	uint width, uint height,
	DXGI_FORMAT format,
	const uint8_t* pInitImage)
{
	HRESULT hr = S_OK;

	ID3D12Resource* pTexResource = nullptr;
	ID3D12Resource* pUploadBuffer = nullptr;

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
		D3D12_RESOURCE_DESC desc = pTexResource->GetDesc();
		D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint;
		uint rows = 0;
		uint64_t rowSize = 0;
		uint64_t totalBytes = 0;

		m_pD3DDevice->GetCopyableFootprints(&desc, 0, 1, 0, &footprint, &rows, &rowSize, &totalBytes);

		uint8_t* pMappedPtr = nullptr;
		CD3DX12_RANGE readRange(0, 0);

		uint64_t uploadBufferSize = GetRequiredIntermediateSize(pTexResource, 0, 1);

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

		const uint8_t* pSrc = pInitImage;
		uint8_t* pDest = pMappedPtr;
		for (uint y = 0; y < height; y++)
		{
			memcpy(pDest, pSrc, static_cast<size_t>(width) * 4);
			pSrc += (width * 4);
			pDest += footprint.Footprint.RowPitch;
		}
		// Unmap
		pUploadBuffer->Unmap(0, nullptr);

		UpdateTextureForWrite(pTexResource, pUploadBuffer);

		SAFE_RELEASE(pUploadBuffer);

	}
	*ppOutResource = pTexResource;

	return true;
}

bool D3D12ResourceManager::CreateTexturePair(
	ID3D12Resource** ppOutResource,
	ID3D12Resource** ppOutUploadBuffer,
	uint width, uint height,
	DXGI_FORMAT format)
{
	HRESULT hr = S_OK;
	ID3D12Resource* pTexResource = nullptr;
	ID3D12Resource* pUploadBuffer = nullptr;

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

bool D3D12ResourceManager::CreateTextureFromFile(
	ID3D12Resource** ppOutResource,
	D3D12_RESOURCE_DESC* pOutDesc,
	const wchar_t* wchFileName,
	bool bUseGpuUploadHeaps)
{
	ASSERT(m_pD3DDevice, "D3D device must be initialized.");
	ASSERT(ppOutResource, "ppOutResource must not be null.");
	ASSERT(wchFileName && *wchFileName, "Invalid filename.");
	*ppOutResource = nullptr;
	if (pOutDesc) std::memset(pOutDesc, 0, sizeof(*pOutDesc));

	// 1) Load via Image
	Image img;
	if (!img.Load(std::filesystem::path(wchFileName))) 
	{
		ASSERT(false, "Image::Load failed.");
		return false;
	}
	ASSERT(img.IsValid(), "Loaded image is invalid.");

	const uint  width = img.GetWidth();
	const uint  height = img.GetHeight();
	const uint  mips = std::max(1u, img.GetMipCount());
	const uint  arraySize = std::max(1u, img.GetArraySize());
	const DXGI_FORMAT dxgiFmt = static_cast<DXGI_FORMAT>(img.GetFormat());

	// 2) Detect 3D volume (depth > 1) by probing slice 1 on mip 0
	bool b3DTex = (img.GetDataPtr(0, 0, 1) != nullptr);
	uint  depth0 = 1;
	if (b3DTex) 
	{
		// count depth at mip 0
		while (img.GetDataPtr(0, 0, depth0) != nullptr) ++depth0;
		ASSERT(depth0 > 1, "3D texture must have depth > 1");
	}

	// 3) Create resource (2D/Cube/Array vs 3D)
	D3D12_RESOURCE_DESC texDesc{};
	if (b3DTex)
	{
		texDesc = {};
		texDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE3D;
		texDesc.Alignment = 0;
		texDesc.Width = static_cast<UINT64>(width);
		texDesc.Height = static_cast<UINT>(height);
		texDesc.DepthOrArraySize = static_cast<UINT16>(depth0);
		texDesc.MipLevels = static_cast<UINT16>(mips);
		texDesc.Format = dxgiFmt;
		texDesc.SampleDesc.Count = 1;
		texDesc.SampleDesc.Quality = 0;
		texDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
		texDesc.Flags = D3D12_RESOURCE_FLAG_NONE;
		// 업로드 힙의 ROW_MAJOR 3D 텍스처는 드물고 제약이 있으니 기본 힙 경유로 고정
		bUseGpuUploadHeaps = false;
	}
	else {
		texDesc = CD3DX12_RESOURCE_DESC::Tex2D(
			dxgiFmt,
			static_cast<UINT64>(width),
			static_cast<UINT>(height),
			static_cast<UINT16>(arraySize),
			static_cast<UINT16>(mips));
		if (bUseGpuUploadHeaps)
		{
			texDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
		}
	}

	const CD3DX12_HEAP_PROPERTIES heapProp(bUseGpuUploadHeaps ? D3D12_HEAP_TYPE_UPLOAD : D3D12_HEAP_TYPE_DEFAULT);
	const D3D12_RESOURCE_STATES initialState = bUseGpuUploadHeaps ? D3D12_RESOURCE_STATE_GENERIC_READ : D3D12_RESOURCE_STATE_COMMON;

	ID3D12Resource* pTexResource = nullptr;
	HRESULT hr = m_pD3DDevice->CreateCommittedResource(
		&heapProp, D3D12_HEAP_FLAG_NONE, &texDesc, initialState, nullptr,
		IID_PPV_ARGS(&pTexResource));
	if (FAILED(hr))
	{
		ASSERT(false, "CreateCommittedResource (texture) failed.");
		return false;
	}

	// 4) Build subresources
	std::vector<D3D12_SUBRESOURCE_DATA> subs;
	subs.reserve(b3DTex ? mips : (arraySize * mips));

	if (b3DTex)
	{
		// 3D: one subresource per mip. pData = slice0; helper copies depth using SlicePitch.
		for (uint mip = 0; mip < mips; ++mip) {
			const void* pData = img.GetDataPtr(0, mip, 0);
			ASSERT(pData, "Image::GetDataPtr (3D) returned null.");
			const size_t rowPitch = img.GetRowPitch(0, mip, 0);
			const size_t slicePitch = img.GetSlicePitch(0, mip, 0);
			ASSERT(rowPitch > 0 && slicePitch >= rowPitch, "Invalid pitches for 3D.");

			D3D12_SUBRESOURCE_DATA s{};
			s.pData = pData;
			s.RowPitch = static_cast<LONG_PTR>(rowPitch);
			s.SlicePitch = static_cast<LONG_PTR>(slicePitch);
			subs.push_back(s);
		}
	}
	else
	{
		// 2D / 2D-array / cubemap-as-array (arraySize may be 6*N)
		for (uint item = 0; item < arraySize; ++item)
		{
			for (uint mip = 0; mip < mips; ++mip)
			{
				const void* pData = img.GetDataPtr(item, mip, 0);
				ASSERT(pData, "Image::GetDataPtr (2D) returned null.");
				const size_t rowPitch = img.GetRowPitch(item, mip, 0);
				const size_t slicePitch = img.GetSlicePitch(item, mip, 0);
				ASSERT(rowPitch > 0 && slicePitch >= rowPitch, "Invalid pitches for 2D.");

				D3D12_SUBRESOURCE_DATA s{};
				s.pData = pData;
				s.RowPitch = static_cast<LONG_PTR>(rowPitch);
				s.SlicePitch = static_cast<LONG_PTR>(slicePitch);
				subs.push_back(s);
			}
		}
	}

	// 5) Upload
	if (bUseGpuUploadHeaps) {
		// CPU write directly into ROW_MAJOR resource (2D only)
		const UINT subCount = static_cast<UINT>(subs.size());
		for (UINT i = 0; i < subCount; ++i)
		{
			const auto& s = subs[i];
			hr = pTexResource->WriteToSubresource(
				i, nullptr, s.pData,
				static_cast<UINT>(s.RowPitch),
				static_cast<UINT>(s.SlicePitch));
			if (FAILED(hr))
			{
				ASSERT(false, "WriteToSubresource failed.");
				SAFE_RELEASE(pTexResource);
				return false;
			}
		}
	}
	else {
		// DEFAULT heap + staging upload
		const UINT subCount = static_cast<UINT>(subs.size());
		const uint64_t uploadSize = GetRequiredIntermediateSize(pTexResource, 0, subCount);

		ID3D12Resource* pUpload = nullptr;
		hr = m_pD3DDevice->CreateCommittedResource(
			&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD),
			D3D12_HEAP_FLAG_NONE,
			&CD3DX12_RESOURCE_DESC::Buffer(uploadSize),
			D3D12_RESOURCE_STATE_GENERIC_READ,
			nullptr,
			IID_PPV_ARGS(&pUpload));
		if (FAILED(hr))
		{
			ASSERT(false, "CreateCommittedResource (upload buffer) failed.");
			SAFE_RELEASE(pTexResource);
			return false;
		}

		hr = m_pCommandAllocator->Reset(); ASSERT(SUCCEEDED(hr), "CA Reset failed.");
		hr = m_pCommandList->Reset(m_pCommandAllocator, nullptr); ASSERT(SUCCEEDED(hr), "CL Reset failed.");

		// COMMON -> COPY_DEST
		m_pCommandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(pTexResource, D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_COPY_DEST));

		UpdateSubresources(m_pCommandList, pTexResource, pUpload, 0, 0, subCount, subs.data());

		// COPY_DEST -> ALL_SHADER_RESOURCE
		m_pCommandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(pTexResource, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_ALL_SHADER_RESOURCE));

		hr = m_pCommandList->Close(); ASSERT(SUCCEEDED(hr), "CL Close failed.");

		ID3D12CommandList* lists[] = { m_pCommandList };
		m_pCommandQueue->ExecuteCommandLists(_countof(lists), lists);
		fence();
		waitForFenceValue();

		SAFE_RELEASE(pUpload);
	}

	if (pOutDesc)
	{
		*pOutDesc = pTexResource->GetDesc();
	}
	*ppOutResource = pTexResource;
	return true;
}

void D3D12ResourceManager::UpdateTextureForWrite(ID3D12Resource* pDestTexResource, ID3D12Resource* pSrcTexResource)
{
	HRESULT hr = S_OK;

	constexpr uint MAX_SUB_RESOURCE_NUM = 32;
	D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint[MAX_SUB_RESOURCE_NUM] = {};
	uint rows[MAX_SUB_RESOURCE_NUM] = {};
	uint64_t rowSizes[MAX_SUB_RESOURCE_NUM] = {};
	uint64_t totalBytes = 0;

	D3D12_RESOURCE_DESC Desc = pDestTexResource->GetDesc();
	ASSERT(Desc.MipLevels <= (uint)_countof(footprint));

	m_pD3DDevice->GetCopyableFootprints(&Desc, 0, Desc.MipLevels, 0, footprint, rows, rowSizes, &totalBytes);

	hr = m_pCommandAllocator->Reset();
	ASSERT(SUCCEEDED(hr), "Failed to Reset CommandAllocator.");

	hr = m_pCommandList->Reset(m_pCommandAllocator, nullptr);
	ASSERT(SUCCEEDED(hr), "Failed to Reset CommandList.");

	m_pCommandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(pDestTexResource, D3D12_RESOURCE_STATE_ALL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_COPY_DEST));
	for (uint i = 0; i < Desc.MipLevels; i++)
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

	SAFE_RELEASE(m_pCommandQueue);

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
	SAFE_CLOSE_HANDLE(m_hFenceEvent);
	SAFE_RELEASE(m_pFence);
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
	SAFE_RELEASE(m_pCommandList);
	SAFE_RELEASE(m_pCommandAllocator);
}

uint64_t D3D12ResourceManager::fence()
{
	m_ui64FenceValue++;
	m_pCommandQueue->Signal(m_pFence, m_ui64FenceValue);
	return m_ui64FenceValue;
}

void D3D12ResourceManager::waitForFenceValue()
{
	const uint64_t expectedFenceValue = m_ui64FenceValue;

	// Wait until the previous frame is finished.
	if (m_pFence->GetCompletedValue() < expectedFenceValue)
	{
		m_pFence->SetEventOnCompletion(expectedFenceValue, m_hFenceEvent);
		WaitForSingleObject(m_hFenceEvent, INFINITE);
	}
}

