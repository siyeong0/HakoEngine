#include "pch.h"
#include "WriteDebugString.h"
#include "D3DUtil.h" 

namespace D3DUtil
{
	void GetHardwareAdapter(IDXGIFactory2* pFactory, IDXGIAdapter1** ppAdapter)
	{
		IDXGIAdapter1* adapter = nullptr;
		*ppAdapter = nullptr;

		for (UINT adapterIndex = 0; DXGI_ERROR_NOT_FOUND != pFactory->EnumAdapters1(adapterIndex, &adapter); ++adapterIndex)
		{
			DXGI_ADAPTER_DESC1 desc;
			adapter->GetDesc1(&desc);

			if (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE)
			{
				// Don't select the Basic Render Driver adapter.
				continue;
			}

			// Check to see if the adapter supports Direct3D 12, but don't create the actual device yet.
			if (SUCCEEDED(D3D12CreateDevice(adapter, D3D_FEATURE_LEVEL_11_0, _uuidof(ID3D12Device), nullptr)))
			{
				break;
			}
		}

		*ppAdapter = adapter;
	}

	void GetSoftwareAdapter(IDXGIFactory2* pFactory, IDXGIAdapter1** ppAdapter)
	{
		IDXGIAdapter1* adapter = nullptr;
		*ppAdapter = nullptr;

		for (UINT adapterIndex = 0; DXGI_ERROR_NOT_FOUND != pFactory->EnumAdapters1(adapterIndex, &adapter); ++adapterIndex)
		{
			DXGI_ADAPTER_DESC1 desc;
			adapter->GetDesc1(&desc);

			if (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE)
			{
				// Check to see if the adapter supports Direct3D 12, but don't create the actual device yet.
				if (SUCCEEDED(D3D12CreateDevice(adapter, D3D_FEATURE_LEVEL_12_1, _uuidof(ID3D12Device), nullptr)))
				{
					*ppAdapter = adapter;
					break;
				}
			}
		}
	}

	bool CheckSupportGpuUploadHeap(ID3D12Device* pD3DDevice)
	{
		bool bResult = false;
		D3D12_FEATURE_DATA_D3D12_OPTIONS16 options16 = {};
		if (SUCCEEDED(pD3DDevice->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS16, &options16, sizeof(options16))))
		{
			if (options16.GPUUploadHeapSupported)
			{
				bResult = true;
			}
		}
		return bResult;
	}

	void SetDebugLayerInfo(ID3D12Device* pD3DDevice)
	{
		ID3D12InfoQueue* pInfoQueue = nullptr;
		pD3DDevice->QueryInterface(IID_PPV_ARGS(&pInfoQueue));
		if (pInfoQueue)
		{
			pInfoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_CORRUPTION, TRUE);
			pInfoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_ERROR, TRUE);

			D3D12_MESSAGE_ID hide[] =
			{
				D3D12_MESSAGE_ID_MAP_INVALID_NULLRANGE,
				D3D12_MESSAGE_ID_UNMAP_INVALID_NULLRANGE,
				// Workarounds for debug layer issues on hybrid-graphics systems
				D3D12_MESSAGE_ID_EXECUTECOMMANDLISTS_WRONGSWAPCHAINBUFFERREFERENCE,
				D3D12_MESSAGE_ID_RESOURCE_BARRIER_MISMATCHING_COMMAND_LIST_TYPE
			};
			D3D12_INFO_QUEUE_FILTER filter = {};
			filter.DenyList.NumIDs = (UINT)_countof(hide);
			filter.DenyList.pIDList = hide;
			pInfoQueue->AddStorageFilterEntries(&filter);

			pInfoQueue->Release();
			pInfoQueue = nullptr;
		}
	}

	void SetDefaultSamplerDesc(D3D12_STATIC_SAMPLER_DESC* pOutSamperDesc, UINT registerIndex)
	{
		D3D12_STATIC_SAMPLER_DESC sampler = {};
		//pOutSamperDesc->Filter = D3D12_FILTER_ANISOTROPIC;
		pOutSamperDesc->Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;

		pOutSamperDesc->AddressU = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
		pOutSamperDesc->AddressV = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
		pOutSamperDesc->AddressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
		pOutSamperDesc->MipLODBias = 0.0f;
		pOutSamperDesc->MaxAnisotropy = 16;
		pOutSamperDesc->ComparisonFunc = D3D12_COMPARISON_FUNC_NEVER;
		pOutSamperDesc->BorderColor = D3D12_STATIC_BORDER_COLOR_OPAQUE_WHITE;
		pOutSamperDesc->MinLOD = -FLT_MAX;
		pOutSamperDesc->MaxLOD = D3D12_FLOAT32_MAX;
		pOutSamperDesc->ShaderRegister = registerIndex;
		pOutSamperDesc->RegisterSpace = 0;
		pOutSamperDesc->ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
	}

	void SetSamplerDesc_Wrap(D3D12_STATIC_SAMPLER_DESC* pOutSamperDesc, UINT RegisterIndex)
	{
		SetDefaultSamplerDesc(pOutSamperDesc, RegisterIndex);
	}

	void SetSamplerDesc_Clamp(D3D12_STATIC_SAMPLER_DESC* pOutSamperDesc, UINT RegisterIndex)
	{
		SetDefaultSamplerDesc(pOutSamperDesc, RegisterIndex);

		pOutSamperDesc->AddressU = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
		pOutSamperDesc->AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
		pOutSamperDesc->AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
	}

	void SetSamplerDesc_Border(D3D12_STATIC_SAMPLER_DESC* pOutSamperDesc, UINT RegisterIndex)
	{
		SetDefaultSamplerDesc(pOutSamperDesc, RegisterIndex);

		pOutSamperDesc->AddressU = D3D12_TEXTURE_ADDRESS_MODE_BORDER;
		pOutSamperDesc->AddressV = D3D12_TEXTURE_ADDRESS_MODE_BORDER;
		pOutSamperDesc->AddressW = D3D12_TEXTURE_ADDRESS_MODE_BORDER;
	}

	void SetSamplerDesc_Mirror(D3D12_STATIC_SAMPLER_DESC* pOutSamperDesc, UINT RegisterIndex)
	{
		SetDefaultSamplerDesc(pOutSamperDesc, RegisterIndex);

		pOutSamperDesc->AddressU = D3D12_TEXTURE_ADDRESS_MODE_MIRROR;
		pOutSamperDesc->AddressV = D3D12_TEXTURE_ADDRESS_MODE_MIRROR;
		pOutSamperDesc->AddressW = D3D12_TEXTURE_ADDRESS_MODE_MIRROR;
	}

	void SerializeAndCreateRaytracingRootSignature(ID3D12Device* pDevice, D3D12_ROOT_SIGNATURE_DESC* pDesc, ID3D12RootSignature** ppOutRootSig)
	{
		ID3DBlob* pBlob = nullptr;
		ID3DBlob* pError = nullptr;

		HRESULT hr = D3D12SerializeRootSignature(pDesc, D3D_ROOT_SIGNATURE_VERSION_1, &pBlob, &pError);
		if (FAILED(hr))
		{
			if (pError)
			{
				const char* szMsg = (const char*)pError->GetBufferPointer();
				WriteDebugStringA(DEBUG_OUTPUT_TYPE_DEBUG_CONSOLE, "%s\n", szMsg);
			}
			__debugbreak();
		}

		//, error ? static_cast<wchar_t*>(error->GetBufferPointer()) : nullptr);
		hr = pDevice->CreateRootSignature(1, pBlob->GetBufferPointer(), pBlob->GetBufferSize(), IID_PPV_ARGS(ppOutRootSig));
		if (FAILED(hr))
			__debugbreak();

		if (pBlob)
		{
			pBlob->Release();
			pBlob = nullptr;
		}
		if (pError)
		{
			pError->Release();
			pError = nullptr;
		}
	}

	HRESULT CreateVertexBuffer(ID3D12Device* pDevice, UINT sizePerVertex, UINT numVertices, D3D12_VERTEX_BUFFER_VIEW* pOutVertexBufferView, ID3D12Resource** ppOutBuffer)
	{
		HRESULT hr = S_OK;

		D3D12_VERTEX_BUFFER_VIEW	VertexBufferView = {};
		ID3D12Resource* pVertexBuffer = nullptr;
		UINT vertexBufferSize = sizePerVertex * numVertices;

		// create vertexbuffer for rendering
		hr = pDevice->CreateCommittedResource(
			&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT),
			D3D12_HEAP_FLAG_NONE,
			&CD3DX12_RESOURCE_DESC::Buffer(vertexBufferSize),
			D3D12_RESOURCE_STATE_COMMON,
			nullptr,
			IID_PPV_ARGS(&pVertexBuffer));

		if (FAILED(hr))
		{
			goto lb_return;
		}

		// Initialize the vertex buffer view.
		VertexBufferView.BufferLocation = pVertexBuffer->GetGPUVirtualAddress();
		VertexBufferView.StrideInBytes = sizePerVertex;
		VertexBufferView.SizeInBytes = vertexBufferSize;

		*pOutVertexBufferView = VertexBufferView;
		*ppOutBuffer = pVertexBuffer;

	lb_return:
		return hr;
	}

	HRESULT CreateUAVBuffer(ID3D12Device* pDevice, UINT64 bufferSize, ID3D12Resource** ppResource, D3D12_RESOURCE_STATES initialResourceState, const WCHAR* wchResourceName)
	{
		auto uploadHeapProperties = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT);
		auto bufferDesc = CD3DX12_RESOURCE_DESC::Buffer(bufferSize, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
		HRESULT hr = pDevice->CreateCommittedResource(
			&uploadHeapProperties,
			D3D12_HEAP_FLAG_NONE,
			&bufferDesc,
			initialResourceState,
			nullptr,
			IID_PPV_ARGS(ppResource));

		if (FAILED(hr))
		{
			goto lb_return;
		}
		if (wchResourceName)
		{
			(*ppResource)->SetName(wchResourceName);
		}
	lb_return:
		return hr;
	}

	HRESULT CreateUploadBuffer(ID3D12Device* pDevice, void* pData, UINT64 DataSize, ID3D12Resource** ppResource, const WCHAR* wchResourceName)
	{
		auto uploadHeapProperties = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD);
		auto bufferDesc = CD3DX12_RESOURCE_DESC::Buffer(DataSize);
		HRESULT hr = pDevice->CreateCommittedResource(
			&uploadHeapProperties,
			D3D12_HEAP_FLAG_NONE,
			&bufferDesc,
			D3D12_RESOURCE_STATE_GENERIC_READ,
			nullptr,
			IID_PPV_ARGS(ppResource));

		if (FAILED(hr))
		{
			goto lb_return;
		}
		if (wchResourceName)
		{
			(*ppResource)->SetName(wchResourceName);
		}
		if (pData)
		{
			void* pMappedData;
			CD3DX12_RANGE readRange(0, 0);
			(*ppResource)->Map(0, &readRange, &pMappedData);
			memcpy(pMappedData, pData, DataSize);
			(*ppResource)->Unmap(0, nullptr);
		}
	lb_return:
		return hr;
	}

	void UpdateTexture(ID3D12Device* pD3DDevice, ID3D12GraphicsCommandList6* pCommandList, ID3D12Resource* pDestTexResource, ID3D12Resource* pSrcTexResource)
	{
		constexpr UINT MAX_SUB_RESOURCE_NUM = 32;
		D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint[MAX_SUB_RESOURCE_NUM] = {};
		UINT rows[MAX_SUB_RESOURCE_NUM] = {};
		UINT64 rowSizes[MAX_SUB_RESOURCE_NUM] = {};
		UINT64 totalBytes = 0;

		D3D12_RESOURCE_DESC desc = pDestTexResource->GetDesc();
		ASSERT(desc.MipLevels <= (UINT)_countof(footprint), "Too many sub resources.");

		pD3DDevice->GetCopyableFootprints(&desc, 0, desc.MipLevels, 0, footprint, rows, rowSizes, &totalBytes);

		pCommandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(pDestTexResource, D3D12_RESOURCE_STATE_ALL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_COPY_DEST));
		for (UINT i = 0; i < desc.MipLevels; i++)
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

			pCommandList->CopyTextureRegion(&destLocation, 0, 0, 0, &srcLocation, nullptr);
		}
		pCommandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(pDestTexResource, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_ALL_SHADER_RESOURCE));
	}
} // namespace D3DUtil