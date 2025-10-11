#include "pch.h"
#include <process.h>
#include <cmath>
#include "Common/ProcessorInfo.h"
#include "BasicMeshObject.h"
#include "CommandListPool.h"
#include "ConstantBufferManager.h"
#include "D3D12ResourceManager.h"
#include "DescriptorPool.h"
#include "FontManager.h"
#include "PSOManager.h"
#include "RayTracingManager.h"
#include "RenderQueueRasterization.h"
#include "RenderQueueRayTracing.h"
#include "RenderThread.h"
#include "RootSignatureManager.h"
#include "ShaderManager.h"
#include "SimpleConstantBufferPool.h"
#include "SingleDescriptorAllocator.h"
#include "SkyObject.h"
#include "SpriteObject.h"
#include "TextureManager.h"

#include "D3D12Renderer.h"

// --------------------------------------------------
// Unknown methods
// --------------------------------------------------

STDMETHODIMP D3D12Renderer::QueryInterface(REFIID refiid, void** ppv)
{
	return E_NOINTERFACE;
}

STDMETHODIMP_(ULONG) D3D12Renderer::AddRef()
{
	m_RefCount++;
	return m_RefCount;
}

STDMETHODIMP_(ULONG) D3D12Renderer::Release()
{
	DWORD ref_count = --m_RefCount;
	if (!m_RefCount)
		delete this;
	return ref_count;
}

// --------------------------------------------------
// IRenderer methods
// --------------------------------------------------

bool ENGINECALL D3D12Renderer::Initialize(
	HWND hWnd,
	bool bEnableRayTracing,
	bool bEnableDebugLayer,
	bool bEnableGBV,
	bool bEnableShaderDebug,
	bool bUseGpuUploadHeaps,
	const WCHAR* wchShaderPath)
{
	HRESULT hr = S_OK;
	ID3D12Debug* pDebugController = nullptr;
	IDXGIFactory4* pFactory = nullptr;
	IDXGIAdapter1* pAdapter = nullptr;
	DXGI_ADAPTER_DESC1 AdapterDesc = {};

	auto cleanupResources = [&]()
		{
			SAFE_RELEASE(pDebugController);
			SAFE_RELEASE(pAdapter);
			SAFE_RELEASE(pFactory);
		};

	uint createFactoryFlags = 0;
	m_DPI = static_cast<float>(GetDpiForWindow(hWnd));

	// If use debug Layer...
	if (bEnableDebugLayer)
	{
		// Enable the D3D12 debug layer.
		if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&pDebugController))))
		{
			pDebugController->EnableDebugLayer();
		}
		createFactoryFlags = DXGI_CREATE_FACTORY_DEBUG;
		if (bEnableGBV)
		{
			ID3D12Debug5* pDebugController5 = nullptr;
			if (S_OK == pDebugController->QueryInterface(IID_PPV_ARGS(&pDebugController5)))
			{
				pDebugController5->SetEnableGPUBasedValidation(TRUE);
				pDebugController5->SetEnableAutoName(TRUE);
				SAFE_RELEASE(pDebugController5);
			}
		}
	}

	// Create DXGIFactory
	CreateDXGIFactory2(createFactoryFlags, IID_PPV_ARGS(&pFactory));

	D3D_FEATURE_LEVEL featureLevels[] =
	{
		D3D_FEATURE_LEVEL_12_2,
		D3D_FEATURE_LEVEL_12_1,
		D3D_FEATURE_LEVEL_12_0,
		D3D_FEATURE_LEVEL_11_1,
		D3D_FEATURE_LEVEL_11_0
	};
	uint featureLevelNum = _countof(featureLevels);
	for (uint featerLevelIndex = 0; featerLevelIndex < featureLevelNum; featerLevelIndex++)
	{
		uint adapterIndex = 0;
		while (DXGI_ERROR_NOT_FOUND != pFactory->EnumAdapters1(adapterIndex, &pAdapter))
		{
			pAdapter->GetDesc1(&AdapterDesc);
			if (SUCCEEDED(D3D12CreateDevice(pAdapter, featureLevels[featerLevelIndex], IID_PPV_ARGS(&m_pD3DDevice))))
			{
				goto lb_exit;
			}
			SAFE_RELEASE(pAdapter);
			adapterIndex++;
		}
	}
lb_exit:

	if (!m_pD3DDevice)
	{
		ASSERT(false, "Failed to create D3D12 Device.");
		cleanupResources();
		return false;
	}

	m_AdapterDesc = AdapterDesc;
	m_hWnd = hWnd;

	if (pDebugController)
	{
		D3DUtil::SetDebugLayerInfo(m_pD3DDevice);
	}

	if (bUseGpuUploadHeaps)
	{
		m_bGpuUploadHeapsEnabled = D3DUtil::CheckSupportGpuUploadHeap(m_pD3DDevice);
	}
	else
	{
		m_bGpuUploadHeapsEnabled = FALSE;
	}

	// Describe and create the command queue.
	{
		D3D12_COMMAND_QUEUE_DESC queueDesc = {};
		queueDesc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
		queueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;

		hr = m_pD3DDevice->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&m_pCommandQueue));
		if (FAILED(hr))
		{
			ASSERT(false, "Failed to create D3D12 Command Queue.");
			cleanupResources();
			return FALSE;
		}
	}

	createDescriptorHeapForRTV();

	RECT rect;
	::GetClientRect(hWnd, &rect);
	uint windowWidth = rect.right - rect.left;
	uint windowHeight = rect.bottom - rect.top;
	uint backBufferWidth = rect.right - rect.left;
	uint backBufferHeight = rect.bottom - rect.top;

	// Describe and create the swap chain.
	{
		DXGI_SWAP_CHAIN_DESC1 swapChainDesc = {};
		swapChainDesc.Width = backBufferWidth;
		swapChainDesc.Height = backBufferHeight;
		swapChainDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
		//swapChainDesc.BufferDesc.RefreshRate.Numerator = m_uiRefreshRate;
		//swapChainDesc.BufferDesc.RefreshRate.Denominator = 1;
		swapChainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
		swapChainDesc.BufferCount = SWAP_CHAIN_FRAME_COUNT;
		swapChainDesc.SampleDesc.Count = 1;
		swapChainDesc.SampleDesc.Quality = 0;
		swapChainDesc.Scaling = DXGI_SCALING_NONE;
		swapChainDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
		swapChainDesc.AlphaMode = DXGI_ALPHA_MODE_IGNORE;
		swapChainDesc.Flags |= DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING;
		m_SwapChainFlags = swapChainDesc.Flags;

		DXGI_SWAP_CHAIN_FULLSCREEN_DESC fsSwapChainDesc = {};
		fsSwapChainDesc.Windowed = TRUE;

		IDXGISwapChain1* pSwapChain1 = nullptr;
		if (FAILED(pFactory->CreateSwapChainForHwnd(m_pCommandQueue, hWnd, &swapChainDesc, &fsSwapChainDesc, nullptr, &pSwapChain1)))
		{
			ASSERT(false, "Failed to create DXGI Swap Chain.");
			return false;
		}
		pSwapChain1->QueryInterface(IID_PPV_ARGS(&m_pSwapChain));
		SAFE_RELEASE(pSwapChain1);
		m_uiRenderTargetIndex = m_pSwapChain->GetCurrentBackBufferIndex();
	}
	m_Viewport.Width = (float)windowWidth;
	m_Viewport.Height = (float)windowHeight;
	m_Viewport.MinDepth = 0.0f;
	m_Viewport.MaxDepth = 1.0f;

	m_ScissorRect.left = 0;
	m_ScissorRect.top = 0;
	m_ScissorRect.right = windowWidth;
	m_ScissorRect.bottom = windowHeight;

	m_Width = windowWidth;
	m_Height = windowHeight;

	// Create frame resources.
	CD3DX12_CPU_DESCRIPTOR_HANDLE rtvHandle(m_pRTVHeap->GetCPUDescriptorHandleForHeapStart());

	// Create a RTV for each frame.
	// Descriptor Table
	// |        0        |        1	       |
	// | Render Target 0 | Render Target 1 |
	for (int n = 0; n < SWAP_CHAIN_FRAME_COUNT; n++)
	{
		m_pSwapChain->GetBuffer(n, IID_PPV_ARGS(&m_pRenderTargets[n]));
		m_pD3DDevice->CreateRenderTargetView(m_pRenderTargets[n], nullptr, rtvHandle);
		rtvHandle.Offset(1, m_rtvDescriptorSize);
	}
	m_srvDescriptorSize = m_pD3DDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

	// Create Depth Stencile resources
	createDescriptorHeapForDSV();
	createDepthStencil(m_Width, m_Height);

	// Create synchronization objects.
	createFence();

	// Create other managers.
	bool bInited = false;

	m_pFontManager = new FontManager;
	bInited = m_pFontManager->Initialize(this, m_pCommandQueue, 1024, 256, bEnableDebugLayer);
	ASSERT(bInited, "FontManager initialization failed.");

	m_pResourceManager = new D3D12ResourceManager;
	bInited = m_pResourceManager->Initialize(m_pD3DDevice);
	ASSERT(bInited, "D3D12ResourceManager initialization failed.");

	m_pTextureManager = new TextureManager;
	bInited = m_pTextureManager->Initialize(this, 1024); // TODO: Use "numExpectedItems" parameter.
	ASSERT(bInited, "TextureManager initialization failed.");

	m_pShaderManager = new ShaderManager;
	bInited = m_pShaderManager->Initialize(this, wchShaderPath, bEnableShaderDebug);
	ASSERT(bInited, "ShaderManager initialization failed.");

	m_pRootSignatureManager = new RootSignatureManager;
	bInited = m_pRootSignatureManager->Initialize(this);
	ASSERT(bInited, "RootSignatureManager initialization failed.");

	m_pPSOManager = new PSOManager;
	bInited = m_pPSOManager->Initialize(this);
	ASSERT(bInited, "PSOManager initialization failed.");

	if (bEnableRayTracing)
	{
		bool bSupportRayTracing = false;
		D3D12_FEATURE_DATA_D3D12_OPTIONS5 featureOptions5 = {};
		if (SUCCEEDED(m_pD3DDevice->CheckFeatureSupport(
			D3D12_FEATURE_D3D12_OPTIONS5,
			&featureOptions5,
			sizeof(featureOptions5))))
		{
			bSupportRayTracing = featureOptions5.RaytracingTier != D3D12_RAYTRACING_TIER_NOT_SUPPORTED;
		}

		if (bSupportRayTracing)
		{
			m_pRayTracingManager = new RayTracingManager;
			bInited = m_pRayTracingManager->Initialize(this, backBufferWidth, backBufferHeight);
			ASSERT(bInited, "RayTracingManager initialization failed.");
		}
		else
		{
			std::cout << "Ray Tracing is not supported on this device.\n";
		}
	}

	uint numPhysicalCores = 0;
	uint numLogicalCores = 0;
	GetPhysicalCoreCount(&numPhysicalCores, &numLogicalCores);
	m_NumRenderThreads = std::min<int>(static_cast<int>(numPhysicalCores), MAX_RENDER_THREAD_COUNT);

#ifdef USE_MULTI_THREAD
	initRenderThreadPool(m_NumRenderThreads);
#endif
	for (int i = 0; i < MAX_PENDING_FRAME_COUNT; i++)
	{
		for (int j = 0; j < m_NumRenderThreads; j++)
		{
			m_ppCommandListPool[i][j] = new CommandListPool;
			m_ppCommandListPool[i][j]->Initialize(m_pD3DDevice, D3D12_COMMAND_LIST_TYPE_DIRECT, 256);

			m_ppDescriptorPool[i][j] = new DescriptorPool;
			m_ppDescriptorPool[i][j]->Initialize(m_pD3DDevice, MAX_DRAW_COUNT_PER_FRAME * BasicMeshObject::MAX_DESCRIPTOR_COUNT_FOR_DRAW);

			m_ppConstBufferManager[i][j] = new ConstantBufferManager;
			m_ppConstBufferManager[i][j]->Initialize(m_pD3DDevice, MAX_DRAW_COUNT_PER_FRAME);
		}
	}
	m_pSingleDescriptorAllocator = new SingleDescriptorAllocator;
	m_pSingleDescriptorAllocator->Initialize(m_pD3DDevice, MAX_DESCRIPTOR_COUNT, D3D12_DESCRIPTOR_HEAP_FLAG_NONE);

	for (int i = 0; i < m_NumRenderThreads; i++)
	{
		m_ppRenderQueueOpaque[i] = new RenderQueueRasterization;
		m_ppRenderQueueOpaque[i]->Initialize(this, NUM_RENDER_QUEUE_ITEMS_OPAQUE);

		m_ppRenderQueueTrasnparent[i] = new RenderQueueRasterization;
		m_ppRenderQueueTrasnparent[i]->Initialize(this, NUM_RENDER_QUEUE_ITEMS_TRANSPARENT);
	}
	m_ppRenderQueueRayTracing[0] = new RenderQueueRayTracing; // TODO: Multi-thread support??
	m_ppRenderQueueRayTracing[0]->Initialize(this, NUM_RENDER_QUEUE_ITEMS_RAYTRACING);

	m_pSkyObject = new SkyObject;
	m_pSkyObject->Initialize(this);

	initCamera();

	wcscpy_s(m_wchShaderPath, wchShaderPath);

	cleanupResources();
	return true;
}

void ENGINECALL D3D12Renderer::Cleanup()
{
#ifdef USE_MULTI_THREAD
	cleanupRenderThreadPool();
#endif

	fence();

	for (int i = 0; i < MAX_PENDING_FRAME_COUNT; i++)
	{
		waitForFenceValue(m_pui64LastFenceValue[i]);
	}
	for (int i = 0; i < m_NumRenderThreads; i++)
	{
		SAFE_DELETE(m_ppRenderQueueOpaque[i]);
		SAFE_DELETE(m_ppRenderQueueTrasnparent[i]);
	}
	SAFE_DELETE(m_ppRenderQueueRayTracing[0]);
	for (int i = 0; i < MAX_PENDING_FRAME_COUNT; i++)
	{
		for (int j = 0; j < m_NumRenderThreads; j++)
		{
			SAFE_DELETE(m_ppCommandListPool[i][j]);
			SAFE_DELETE(m_ppConstBufferManager[i][j]);
			SAFE_DELETE(m_ppDescriptorPool[i][j]);
		}
	}

	SAFE_DELETE(m_pSkyObject);

	SAFE_DELETE(m_pTextureManager);
	SAFE_DELETE(m_pResourceManager);
	SAFE_DELETE(m_pFontManager);
	SAFE_DELETE(m_pShaderManager);
	SAFE_DELETE(m_pRootSignatureManager);
	SAFE_DELETE(m_pPSOManager);
	SAFE_DELETE(m_pRayTracingManager);

	SAFE_DELETE(m_pSingleDescriptorAllocator);

	cleanupDescriptorHeapForRTV();
	cleanupDescriptorHeapForDSV();

	for (int i = 0; i < SWAP_CHAIN_FRAME_COUNT; i++)
	{
		SAFE_RELEASE(m_pRenderTargets[i]);
	}
	SAFE_RELEASE(m_pDepthStencil);
	SAFE_RELEASE(m_pSwapChain);
	SAFE_RELEASE(m_pCommandQueue);

	cleanupFence();

	if (m_pD3DDevice)
	{
		// Release the device
		const ULONG remainingRefCount = m_pD3DDevice->Release();

		if (remainingRefCount > 0)
		{
			// Potential resource leak: device still has references!
#if defined(_DEBUG)
			Microsoft::WRL::ComPtr<IDXGIDebug1> dxgiDebug;
			if (SUCCEEDED(DXGIGetDebugInterface1(0, IID_PPV_ARGS(&dxgiDebug))))
			{
				dxgiDebug->ReportLiveObjects(DXGI_DEBUG_ALL, DXGI_DEBUG_RLO_SUMMARY);
			}
#endif
			ASSERT(false, "D3D12 Device reference count is not zero (resource leak).");
		}
		m_pD3DDevice = nullptr;
	}
}

void ENGINECALL D3D12Renderer::Update(float dt)
{
	m_PerFrameCB.LightDir = FLOAT3(-0.577f, -0.577f, -0.577f);
	m_PerFrameCB.LightColor = FLOAT3(1.0f, 1.0f, 1.0f);
	m_PerFrameCB.Ambient = FLOAT3(0.3f, 0.3f, 0.3f);

	m_PerFrameCB.Near = NEAR_Z;
	m_PerFrameCB.Far = FAR_Z;
	m_PerFrameCB.MaxRadianceRayRecursionDepth = 1;
}

void ENGINECALL D3D12Renderer::BeginRender()
{
	//
	// Clear render target and initialize render data.
	//

	// Select and allocate command list
	CommandListPool* pCommandListPool = m_ppCommandListPool[m_CurrContextIndex][0];
	ID3D12GraphicsCommandList6* pCommandList = pCommandListPool->GetCurrentCommandList();

	// Change ResourceState Present to RenderTarget
	pCommandList->ResourceBarrier(1,
		&CD3DX12_RESOURCE_BARRIER::Transition(
			m_pRenderTargets[m_uiRenderTargetIndex],
			D3D12_RESOURCE_STATE_PRESENT,
			D3D12_RESOURCE_STATE_RENDER_TARGET));

	// Clear render taget
	CD3DX12_CPU_DESCRIPTOR_HANDLE rtvHandle(m_pRTVHeap->GetCPUDescriptorHandleForHeapStart(), m_uiRenderTargetIndex, m_rtvDescriptorSize);
	CD3DX12_CPU_DESCRIPTOR_HANDLE dsvHandle(m_pDSVHeap->GetCPUDescriptorHandleForHeapStart());

	const float BackColor[] = { 0.0f, 0.0f, 1.0f, 1.0f };
	pCommandList->ClearRenderTargetView(rtvHandle, BackColor, 0, nullptr);
	pCommandList->ClearDepthStencilView(dsvHandle, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);

	pCommandList = pCommandListPool->GetCurrentCommandList();
	pCommandList->RSSetViewports(1, &m_Viewport);
	pCommandList->RSSetScissorRects(1, &m_ScissorRect);
	pCommandList->OMSetRenderTargets(1, &rtvHandle, FALSE, &dsvHandle);
	m_pSkyObject->Draw(0, pCommandList);

	// Execute immediatey
	pCommandListPool->CloseAndExecute(m_pCommandQueue);

	fence();
}

void ENGINECALL D3D12Renderer::EndRender()
{
	CommandListPool* pCommandListPool = m_ppCommandListPool[m_CurrContextIndex][0]; // the command list pool currently in use.

	// Set RenderTarget to process the rendering queue.
	CD3DX12_CPU_DESCRIPTOR_HANDLE rtvHandle(m_pRTVHeap->GetCPUDescriptorHandleForHeapStart(), m_uiRenderTargetIndex, m_rtvDescriptorSize);
	CD3DX12_CPU_DESCRIPTOR_HANDLE dsvHandle(m_pDSVHeap->GetCPUDescriptorHandleForHeapStart());

	// TODO: Combine raytracing output and rasterized output.

	// Do raytracing and copy the output to the back buffer.
	if (IsRayTracingEnabledInl())
	{
		m_ppRenderQueueRayTracing[0]->Process(0, pCommandListPool, m_pCommandQueue, 400, rtvHandle, dsvHandle, &m_Viewport, &m_ScissorRect);

		if (m_pRayTracingManager->IsUpdatedAccelerationStructure())
		{
			for (uint i = 0; i < MAX_PENDING_FRAME_COUNT; i++)
			{
				waitForFenceValue(m_pui64LastFenceValue[i]);
			}
			m_pRayTracingManager->UpdateAccelerationStructure();
		}

		ID3D12GraphicsCommandList6* pCommandList = pCommandListPool->GetCurrentCommandList();

		// const float BackColor[] = { 0.0f, 0.0f, 1.0f, 1.0f };
		// pCommandList->ClearRenderTargetView(rtvHandle, BackColor, 0, nullptr);
		// pCommandList->ClearDepthStencilView(dsvHandle, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);

		pCommandList->RSSetViewports(1, &m_Viewport);
		pCommandList->RSSetScissorRects(1, &m_ScissorRect);
		pCommandList->OMSetRenderTargets(1, &rtvHandle, FALSE, &dsvHandle);

		m_pRayTracingManager->DoRaytracing(pCommandList);
		ID3D12Resource* pRayTracingOuputResource = m_pRayTracingManager->GetOutputResource();

		D3D12_RESOURCE_BARRIER preCopyBarriers[2];
		preCopyBarriers[0] = CD3DX12_RESOURCE_BARRIER::Transition(m_pRenderTargets[m_uiRenderTargetIndex], D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_COPY_DEST);
		preCopyBarriers[1] = CD3DX12_RESOURCE_BARRIER::Transition(pRayTracingOuputResource, D3D12_RESOURCE_STATE_ALL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_COPY_SOURCE);
		pCommandList->ResourceBarrier(ARRAYSIZE(preCopyBarriers), preCopyBarriers);

		pCommandList->CopyResource(m_pRenderTargets[m_uiRenderTargetIndex], pRayTracingOuputResource);

		D3D12_RESOURCE_BARRIER postCopyBarriers[2];
		postCopyBarriers[0] = CD3DX12_RESOURCE_BARRIER::Transition(m_pRenderTargets[m_uiRenderTargetIndex], D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_RENDER_TARGET);
		postCopyBarriers[1] = CD3DX12_RESOURCE_BARRIER::Transition(pRayTracingOuputResource, D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_ALL_SHADER_RESOURCE);
		pCommandList->ResourceBarrier(ARRAYSIZE(postCopyBarriers), postCopyBarriers);

		// Present
		// pCommandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(m_pRenderTargets[m_uiRenderTargetIndex], D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT));
		pCommandListPool->CloseAndExecute(m_pCommandQueue);
	}

	// if (!IsRayTracingEnabledInl())
	{
#ifdef USE_MULTI_THREAD
		// ---- Phase 1: Opaque ----
		m_RenderPhase.store(RENDER_PASS_OPAQUE, std::memory_order_relaxed);
		m_lActiveThreadCount = m_NumRenderThreads;
		for (int i = 0; i < m_NumRenderThreads; i++)
		{
			SetEvent(m_pThreadDescList[i].hEventList[RENDER_THREAD_EVENT_TYPE_PROCESS]);
		}
		WaitForSingleObject(m_hCompleteEvent, INFINITE);
		// ---- Phase 2: Transparent ----
		m_RenderPhase.store(RENDER_PASS_TRANSPARENT, std::memory_order_relaxed);
		m_lActiveThreadCount = m_NumRenderThreads;
		for (int i = 0; i < m_NumRenderThreads; i++)
		{
			SetEvent(m_pThreadDescList[i].hEventList[RENDER_THREAD_EVENT_TYPE_PROCESS]);
		}
		WaitForSingleObject(m_hCompleteEvent, INFINITE);
		// TODO: OIT support.
#else
		// Each CommandList processes 400 items.
		for (int i = 0; i < m_NumRenderThreads; i++)
		{
			m_ppRenderQueueOpaque[i]->Process(i, pCommandListPool, m_pCommandQueue, 400, rtvHandle, dsvHandle, &m_Viewport, &m_ScissorRect);
		}
		for (int i = 0; i < m_NumRenderThreads; i++)
		{
			m_ppRenderQueueTrasnparent[i]->Process(i, pCommandListPool, m_pCommandQueue, 400, rtvHandle, dsvHandle, &m_Viewport, &m_ScissorRect);
		}
#endif	
	}

	// Present
	ID3D12GraphicsCommandList6* pCommandList = pCommandListPool->GetCurrentCommandList();
	pCommandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(m_pRenderTargets[m_uiRenderTargetIndex], D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT));

	pCommandListPool->CloseAndExecute(m_pCommandQueue);

	// Reset all render queues
	for (int i = 0; i < m_NumRenderThreads; i++)
	{
		m_ppRenderQueueOpaque[i]->Reset();
		m_ppRenderQueueTrasnparent[i]->Reset();
	}
	m_ppRenderQueueRayTracing[0]->Reset();
}

void ENGINECALL D3D12Renderer::Present()
{
	fence();
	// Transfer the Back Buffer to the Primary Buffer.

	//uint m_SyncInterval = 1;	// VSync On
	uint m_SyncInterval = 0;	// VSync Off

	uint uiSyncInterval = m_SyncInterval;
	uint uiPresentFlags = 0;

	if (!uiSyncInterval)
	{
		uiPresentFlags = DXGI_PRESENT_ALLOW_TEARING;
	}

	HRESULT hr = m_pSwapChain->Present(uiSyncInterval, uiPresentFlags);
	ASSERT(hr != DXGI_ERROR_DEVICE_REMOVED, "The GPU device instance has been suspended. Use GetDeviceRemovedReason to determine the appropriate action.");

	m_uiRenderTargetIndex = m_pSwapChain->GetCurrentBackBufferIndex();

	// Prepare next frame.
	int nextContextIndex = (m_CurrContextIndex + 1) % MAX_PENDING_FRAME_COUNT;
	waitForFenceValue(m_pui64LastFenceValue[nextContextIndex]);

	// Reset resources per frame.
	for (int i = 0; i < m_NumRenderThreads; i++)
	{
		m_ppConstBufferManager[nextContextIndex][i]->Reset();
		m_ppDescriptorPool[nextContextIndex][i]->Reset();
		m_ppCommandListPool[nextContextIndex][i]->Reset();
	}
	m_CurrContextIndex = nextContextIndex;
}

bool ENGINECALL D3D12Renderer::IsRayTracingEnabled() const
{
	return IsRayTracingEnabledInl();
}

void ENGINECALL D3D12Renderer::RenderMeshObject(
	IMeshObject* pMeshObj,
	const Matrix4x4* pMatWorld)
{
	RenderItem item = {};
	item.Type = RENDER_ITEM_TYPE_MESH_OBJ;
	item.pObjHandle = pMeshObj;
	item.MeshObjParam.matWorld = *pMatWorld;

	bool bAdded = false;
	switch (pMeshObj->GetRenderPass())
	{
	case RENDER_PASS_OPAQUE:
		bAdded = m_ppRenderQueueOpaque[m_CurrThreadIndex]->Add(&item);
		ASSERT(bAdded, "Render Queue is full.");
		break;
	case RENDER_PASS_TRANSPARENT:
		bAdded = m_ppRenderQueueTrasnparent[m_CurrThreadIndex]->Add(&item);
		ASSERT(bAdded, "Render Queue Transparent is full.");
		break;
	case RENDER_PASS_RAYTRACING_OPAQUE:
		bAdded = m_ppRenderQueueRayTracing[0]->Add(&item);
		ASSERT(bAdded, "Render Queue is full.");
		break;
	case RENDER_PASS_RAYTRACING_TRANSPARENT:
		// TODO: Support transparent object in raytracing.
		ASSERT(false, "Raytracing Transparent is not supported yet.");
		break;
	default:
		ASSERT(false, "Invalid render pass.");
		break;
	}
	ASSERT(bAdded, "Render Queue is full. or Invalid render pass.");

	m_CurrThreadIndex++;
	m_CurrThreadIndex = m_CurrThreadIndex % m_NumRenderThreads;
}

void ENGINECALL D3D12Renderer::RenderSpriteWithTex(
	void* pSprObjHandle,
	int posX, int posY,
	float scaleX, float scaleY,
	const RECT* pRect,
	float z,
	void* pTexHandle)
{
	RenderItem item = {};
	item.Type = RENDER_ITEM_TYPE_SPRITE;
	item.pObjHandle = pSprObjHandle;
	item.SpriteParam.PosX = posX;
	item.SpriteParam.PosY = posY;
	item.SpriteParam.ScaleX = scaleX;
	item.SpriteParam.ScaleY = scaleY;

	if (pRect)
	{
		item.SpriteParam.bUseRect = true;
		item.SpriteParam.Rect = *pRect;
	}
	else
	{
		item.SpriteParam.bUseRect = false;
		item.SpriteParam.Rect = {};
	}
	item.SpriteParam.pTexHandle = pTexHandle;
	item.SpriteParam.Z = z;

	// Always add to Transparent queue.
	bool bAdded = m_ppRenderQueueTrasnparent[m_CurrThreadIndex]->Add(&item);;

	m_CurrThreadIndex++;
	m_CurrThreadIndex = m_CurrThreadIndex % m_NumRenderThreads;
}

// TODO: Support rotation. Get Transform info.
void ENGINECALL D3D12Renderer::RenderSprite(
	void* pSprObjHandle,
	int posX, int posY,
	float scaleX, float scaleY,
	float z)
{
	RenderSpriteWithTex(pSprObjHandle, posX, posY, scaleX, scaleY, nullptr, z, nullptr);
}

IMeshObject* ENGINECALL D3D12Renderer::CreateBasicMeshObject()
{
	BasicMeshObject* pMeshObj = new BasicMeshObject;
	pMeshObj->Initialize(this);
	return pMeshObj;
}

ISprite* ENGINECALL D3D12Renderer::CreateSpriteObject()
{
	SpriteObject* pSprObj = new SpriteObject;
	pSprObj->Initialize(this);

	return pSprObj;
}

ISprite* ENGINECALL D3D12Renderer::CreateSpriteObject(const WCHAR* wchTexFileName)
{
	SpriteObject* pSprObj = new SpriteObject;

	pSprObj->Initialize(this, wchTexFileName, nullptr);

	return pSprObj;
}

ISprite* ENGINECALL D3D12Renderer::CreateSpriteObject(const WCHAR* wchTexFileName, int posX, int posY, int width, int height)
{
	SpriteObject* pSprObj = new SpriteObject;

	RECT rect = {};
	rect.left = posX;
	rect.top = posY;
	rect.right = width;
	rect.bottom = height;
	pSprObj->Initialize(this, wchTexFileName, &rect);

	return pSprObj;
}

void* ENGINECALL D3D12Renderer::CreateTiledTexture(uint texWidth, uint texHeight, uint8_t r, uint8_t g, uint8_t b)
{
	ASSERT(m_pTextureManager, "Texture manager is not initialized.");
	ASSERT(texWidth > 0 && texHeight > 0, "Invalid parameters.");

	DXGI_FORMAT texFormat = DXGI_FORMAT_R8G8B8A8_UNORM;

	uint8_t* image = (uint8_t*)malloc(texWidth * texHeight * 4);
	memset(image, 0, (size_t)texWidth * texHeight * 4);

	const RGBA WHITE = { 255, 255, 255, 255 };
	const RGBA BLACK = { 0, 0, 0, 255 };
	for (uint y = 0; y < texHeight; y++)
	{
		for (uint x = 0; x < texWidth; x++)
		{
			RGBA* pDest = reinterpret_cast<RGBA*>(image + (x + y * texWidth) * 4);
			*pDest = (((x ^ y) & 1) == 0) ? WHITE : BLACK;
		}
	}
	TextureHandle* pTexHandle = m_pTextureManager->CreateImmutableTexture(texWidth, texHeight, texFormat, image);

	SAFE_FREE(image);

	return pTexHandle;
}

void* ENGINECALL D3D12Renderer::CreateDynamicTexture(uint texWidth, uint texHeight)
{
	TextureHandle* pTexHandle = m_pTextureManager->CreateDynamicTexture(texWidth, texHeight);
	return pTexHandle;
}

void* ENGINECALL D3D12Renderer::CreateTextureFromFile(const WCHAR* wchFileName)
{
	TextureHandle* pTexHandle = m_pTextureManager->CreateTextureFromFile(wchFileName);
	ASSERT(pTexHandle, "Failed to create texture from file.");
	return pTexHandle;
}

void ENGINECALL D3D12Renderer::UpdateTextureWithImage(void* pTexHandle, const uint8_t* pSrcBits, uint srcWidth, uint srcHeight)
{
	TextureHandle* pTextureHandle = (TextureHandle*)pTexHandle;
	ID3D12Resource* pDestTexResource = pTextureHandle->pTexResource;
	ID3D12Resource* pUploadBuffer = pTextureHandle->pUploadBuffer;

	D3D12_RESOURCE_DESC Desc = pDestTexResource->GetDesc();
	ASSERT(srcWidth <= Desc.Width, "Source width is too large for the destination texture.");
	ASSERT(srcHeight <= Desc.Height, "Source width is too large for the destination texture.");

	D3D12_PLACED_SUBRESOURCE_FOOTPRINT Footprint;
	uint rows = 0;
	uint64_t rowSize = 0;
	uint64_t totalBytes = 0;

	m_pD3DDevice->GetCopyableFootprints(&Desc, 0, 1, 0, &Footprint, &rows, &rowSize, &totalBytes);

	uint8_t* pMappedPtr = nullptr;
	CD3DX12_RANGE readRange(0, 0);

	HRESULT hr = pUploadBuffer->Map(0, &readRange, reinterpret_cast<void**>(&pMappedPtr));
	ASSERT(SUCCEEDED(hr), "Failed to map the upload buffer.");

	const uint8_t* pSrc = pSrcBits;
	uint8_t* pDest = pMappedPtr;
	for (uint y = 0; y < srcHeight; y++)
	{
		memcpy(pDest, pSrc, (size_t)srcWidth * 4);
		pSrc += (srcWidth * 4);
		pDest += Footprint.Footprint.RowPitch;
	}
	// Unmap
	pUploadBuffer->Unmap(0, nullptr);

	pTextureHandle->bUpdated = true;
}

void ENGINECALL D3D12Renderer::DeleteTexture(void* pTexHandle)
{
	// wait for all commands
	for (uint i = 0; i < MAX_PENDING_FRAME_COUNT; i++)
	{
		waitForFenceValue(m_pui64LastFenceValue[i]);
	}
	m_pTextureManager->DeleteTexture((TextureHandle*)pTexHandle);
}

void* ENGINECALL D3D12Renderer::CreateFontObject(const WCHAR* wchFontFamilyName, float fontSize)
{
	FontHandle* pFontHandle = m_pFontManager->CreateFontObject(wchFontFamilyName, fontSize);
	return pFontHandle;
}

void ENGINECALL D3D12Renderer::DeleteFontObject(void* pFontHandle)
{
	m_pFontManager->DeleteFontObject((FontHandle*)pFontHandle);
}

bool ENGINECALL D3D12Renderer::WriteTextToBitmap(uint8_t* dstImage, uint dstWidth, uint dstHeight, uint dstPitch, int* outWidth, int* outHeight, void* pFontObjHandle, const WCHAR* wchString, uint len)
{
	bool bResult = m_pFontManager->WriteTextToBitmap(dstImage, dstWidth, dstHeight, dstPitch, outWidth, outHeight, (FontHandle*)pFontObjHandle, wchString, len);
	return bResult;
}

bool ENGINECALL D3D12Renderer::UpdateWindowSize(uint32_t backBufferWidth, uint32_t backBufferHeight)
{
	if ((backBufferWidth == 0 || backBufferHeight == 0) ||				// Zero size can be given when the window is minimized.
		(m_Width == backBufferWidth && m_Height == backBufferHeight))	// Size is not changed.
	{
		return true;
	}

	// wait for all commands
	fence();

	for (int i = 0; i < MAX_PENDING_FRAME_COUNT; i++)
	{
		waitForFenceValue(m_pui64LastFenceValue[i]);
	}

	DXGI_SWAP_CHAIN_DESC1	desc;
	HRESULT	hr = m_pSwapChain->GetDesc1(&desc);
	if (FAILED(hr))
	{
		ASSERT(false, "Failed to get Swap Chain Desc.");
		return false;
	}

	for (int n = 0; n < SWAP_CHAIN_FRAME_COUNT; n++)
	{
		SAFE_RELEASE(m_pRenderTargets[n]);
	}

	SAFE_RELEASE(m_pDepthStencil);

	if (FAILED(m_pSwapChain->ResizeBuffers(SWAP_CHAIN_FRAME_COUNT, backBufferWidth, backBufferHeight, DXGI_FORMAT_R8G8B8A8_UNORM, m_SwapChainFlags)))
	{
		ASSERT(false, "Failed to resize Swap Chain buffers.");
		return false;
	}

	m_uiRenderTargetIndex = m_pSwapChain->GetCurrentBackBufferIndex();

	// Create frame resources.
	CD3DX12_CPU_DESCRIPTOR_HANDLE rtvHandle(m_pRTVHeap->GetCPUDescriptorHandleForHeapStart());

	// Create a RTV for each frame.
	for (int n = 0; n < SWAP_CHAIN_FRAME_COUNT; n++)
	{
		m_pSwapChain->GetBuffer(n, IID_PPV_ARGS(&m_pRenderTargets[n]));
		m_pD3DDevice->CreateRenderTargetView(m_pRenderTargets[n], nullptr, rtvHandle);
		rtvHandle.Offset(1, m_rtvDescriptorSize);
	}

	createDepthStencil(backBufferWidth, backBufferHeight);

	m_Width = backBufferWidth;
	m_Height = backBufferHeight;
	m_Viewport.Width = (float)m_Width;
	m_Viewport.Height = (float)m_Height;
	m_ScissorRect.left = 0;
	m_ScissorRect.top = 0;
	m_ScissorRect.right = m_Width;
	m_ScissorRect.bottom = m_Height;

	m_pRayTracingManager->UpdateWindowSize(m_Width, m_Height);

	initCamera();

	return true;
}

void ENGINECALL D3D12Renderer::SetCameraPos(float x, float y, float z)
{
	m_CamPos = FLOAT3(x, y, z);
	updateCamera();
}

void ENGINECALL D3D12Renderer::SetCameraRot(float yaw, float pitch, float roll)
{
	m_fCamYaw += yaw;
	m_fCamPitch += pitch;
	m_fCamRoll += roll;

	updateCamera();
}

void ENGINECALL D3D12Renderer::MoveCamera(float x, float y, float z)
{
	FLOAT3 camMoveForward = m_CamDir * z;
	FLOAT3 camMoveRight = m_CamRight * x;
	FLOAT3 camMoveUp = m_CamUp * y;

	m_CamPos += camMoveForward;
	m_CamPos += camMoveRight;
	m_CamPos += camMoveUp;

	updateCamera();
}

FLOAT3 ENGINECALL D3D12Renderer::GetCameraPos()
{
	return m_CamPos;
}

int ENGINECALL D3D12Renderer::GetCommandListCount()
{
	size_t numCmdLists = 0;
	for (int i = 0; i < MAX_PENDING_FRAME_COUNT; i++)
	{
		for (int j = 0; j < m_NumRenderThreads; j++)
		{
			numCmdLists += m_ppCommandListPool[i][j]->GetTotalNumCmdList();
		}
	}
	return static_cast<int>(numCmdLists);
}

bool ENGINECALL D3D12Renderer::IsGpuUploadHeapsEnabled() const
{
	return m_bGpuUploadHeapsEnabled;
}

// --------------------------------------------------
// Internal methods
// --------------------------------------------------

D3D12Renderer::~D3D12Renderer()
{
	Cleanup();
}

void D3D12Renderer::ProcessByThread(int threadIndex)
{
	CommandListPool* pCommandListPool = m_ppCommandListPool[m_CurrContextIndex][threadIndex];	// 현재 사용중인 command list pool

	// Set RenderTarget to process the rendering queue.
	CD3DX12_CPU_DESCRIPTOR_HANDLE rtvHandle(m_pRTVHeap->GetCPUDescriptorHandleForHeapStart(), m_uiRenderTargetIndex, m_rtvDescriptorSize);
	CD3DX12_CPU_DESCRIPTOR_HANDLE dsvHandle(m_pDSVHeap->GetCPUDescriptorHandleForHeapStart());

	constexpr int NUM_ITEMS_PER_PROCESS = 400;
	switch (m_RenderPhase.load(std::memory_order_relaxed))
	{
	case RENDER_PASS_OPAQUE:
		m_ppRenderQueueOpaque[threadIndex]->Process(
			threadIndex, pCommandListPool, m_pCommandQueue, NUM_ITEMS_PER_PROCESS, rtvHandle, dsvHandle, &m_Viewport, &m_ScissorRect);
		break;
	case RENDER_PASS_TRANSPARENT:
		m_ppRenderQueueTrasnparent[threadIndex]->Process(
			threadIndex, pCommandListPool, m_pCommandQueue, NUM_ITEMS_PER_PROCESS, rtvHandle, dsvHandle, &m_Viewport, &m_ScissorRect);
		break;
	case RENDER_PASS_RAYTRACING_OPAQUE:
		// TODO: Raytracing support??
		ASSERT(false, "Raytracing is not supported in multi-thread rendering.");
		break;
	case RENDER_PASS_RAYTRACING_TRANSPARENT:
		// TODO: Raytracing support??
		ASSERT(false, "Raytracing is not supported in multi-thread rendering.");
		break;
	default:
		ASSERT(false, "Invalid render pass.");
		break;
	}

	LONG currCount = _InterlockedDecrement(&m_lActiveThreadCount);
	if (0 == currCount)
	{
		SetEvent(m_hCompleteEvent);
	}
}

void D3D12Renderer::EnsureCompleted()
{
	// Wait for all commands.
	for (int i = 0; i < MAX_PENDING_FRAME_COUNT; i++)
	{
		waitForFenceValue(m_pui64LastFenceValue[i]);
	}
}

SimpleConstantBufferPool* D3D12Renderer::GetConstantBufferPool(CONSTANT_BUFFER_TYPE type, int threadIndex) const
{
	ConstantBufferManager* pConstBufferManager = m_ppConstBufferManager[m_CurrContextIndex][threadIndex];
	SimpleConstantBufferPool* pConstBufferPool = pConstBufferManager->GetConstantBufferPool(type);
	return pConstBufferPool;
}

void D3D12Renderer::GetViewProjMatrix(Matrix4x4* outMatView, Matrix4x4* outMatProj) const
{
	*outMatView = XMMatrixTranspose(m_PerFrameCB.View);
	*outMatProj = XMMatrixTranspose(m_PerFrameCB.Proj);
}

void D3D12Renderer::SetCurrentPathForShader() const
{
	GetCurrentDirectory(_MAX_PATH, (LPWSTR)m_wchCurrentPathBackup);
	SetCurrentDirectory(m_wchShaderPath);
}

void D3D12Renderer::RestoreCurrentPath() const
{
	SetCurrentDirectory(m_wchCurrentPathBackup);
}

// --------------------------------------------------
// Private methods
// --------------------------------------------------

void D3D12Renderer::initCamera()
{
	m_fCamYaw = 0.0f;
	m_fCamPitch = 0.0f;
	m_fCamRoll = 0.0f;

	updateCamera();
}

void D3D12Renderer::updateCamera()
{
	FLOAT3 xAxis = { 1.0f, 0.0f, 0.0f };
	FLOAT3 yAxis = { 0.0f, 1.0f, 0.0f };
	FLOAT3 zAxis = { 0.0f, 0.0f, 1.0f };

	Matrix4x4 matRotPitch = DirectX::XMMatrixRotationX(m_fCamPitch);
	Matrix4x4 matRotYaw = DirectX::XMMatrixRotationY(m_fCamYaw);

	Matrix4x4 matCamRot = XMMatrixMultiply(matRotPitch, matRotYaw);

	DirectX::XMVECTOR vCamDir = DirectX::XMVector3Transform(DirectX::XMVectorSet(zAxis.x, zAxis.y, zAxis.z, 0.0f), matCamRot);
	m_CamDir = FLOAT3::Normalize({ DirectX::XMVectorGetX(vCamDir), DirectX::XMVectorGetY(vCamDir), DirectX::XMVectorGetZ(vCamDir) });
	m_CamRight = FLOAT3::Normalize(FLOAT3::Cross(yAxis, m_CamDir));
	m_CamUp = FLOAT3::Normalize(FLOAT3::Cross(m_CamDir, m_CamRight));

	// View matrix
	Matrix4x4 view = DirectX::XMMatrixLookToLH(
		DirectX::XMVectorSet(m_CamPos.x, m_CamPos.y, m_CamPos.z, 1.0f),
		DirectX::XMVectorSet(m_CamDir.x, m_CamDir.y, m_CamDir.z, 0.0f),
		DirectX::XMVectorSet(m_CamUp.x, m_CamUp.y, m_CamUp.z, 0.0f)
	);

	// Proj matrix
	float fovY = DirectX::XM_PIDIV4; // (rad)
	float aspectRatio = (float)m_Width / (float)m_Height;
	Matrix4x4 proj = DirectX::XMMatrixPerspectiveFovLH(fovY, aspectRatio, NEAR_Z, FAR_Z);

	// View x Proj matrix
	Matrix4x4 viewProj = XMMatrixMultiply(view, proj);

	// Inverse matrices
	DirectX::XMVECTOR det;
	Matrix4x4 invView = XMMatrixInverse(&det, view);
	ASSERT(det.m128_f32[0] != 0.0f, "Matrix is not invertible.");
	Matrix4x4 invProj = XMMatrixInverse(&det, proj);
	ASSERT(det.m128_f32[0] != 0.0f, "Matrix is not invertible.");
	Matrix4x4 invViewProj = XMMatrixInverse(&det, viewProj);
	ASSERT(det.m128_f32[0] != 0.0f, "Matrix is not invertible.");

	// Store to the constant buffer.
	m_PerFrameCB.View = DirectX::XMMatrixTranspose(view);
	m_PerFrameCB.Proj = DirectX::XMMatrixTranspose(proj);
	m_PerFrameCB.ViewProj = DirectX::XMMatrixTranspose(viewProj);
	m_PerFrameCB.InvView = DirectX::XMMatrixTranspose(invView);
	m_PerFrameCB.InvProj = DirectX::XMMatrixTranspose(invProj);
	m_PerFrameCB.InvViewProj = DirectX::XMMatrixTranspose(invViewProj);
}

bool D3D12Renderer::createDepthStencil(int width, int height)
{
	// Create the depth stencil view.
	D3D12_DEPTH_STENCIL_VIEW_DESC depthStencilDesc = {};
	depthStencilDesc.Format = DXGI_FORMAT_D32_FLOAT;
	depthStencilDesc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
	depthStencilDesc.Flags = D3D12_DSV_FLAG_NONE;

	D3D12_CLEAR_VALUE depthOptimizedClearValue = {};
	depthOptimizedClearValue.Format = DXGI_FORMAT_D32_FLOAT;
	depthOptimizedClearValue.DepthStencil.Depth = 1.0f;
	depthOptimizedClearValue.DepthStencil.Stencil = 0;

	CD3DX12_RESOURCE_DESC depthDesc(
		D3D12_RESOURCE_DIMENSION_TEXTURE2D,
		0,
		width,
		height,
		1,
		1,
		DXGI_FORMAT_R32_TYPELESS,
		1,
		0,
		D3D12_TEXTURE_LAYOUT_UNKNOWN,
		D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL);

	if (FAILED(m_pD3DDevice->CreateCommittedResource(
		&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT),
		D3D12_HEAP_FLAG_NONE,
		&depthDesc,
		D3D12_RESOURCE_STATE_DEPTH_WRITE,
		&depthOptimizedClearValue,
		IID_PPV_ARGS(&m_pDepthStencil)
	)))
	{
		ASSERT(false, "Failed to create Depth Stencil View.");
		return false;
	}
	m_pDepthStencil->SetName(L"D3D12Renderer::m_pDepthStencil");

	CD3DX12_CPU_DESCRIPTOR_HANDLE	dsvHandle(m_pDSVHeap->GetCPUDescriptorHandleForHeapStart());
	m_pD3DDevice->CreateDepthStencilView(m_pDepthStencil, &depthStencilDesc, dsvHandle);

	return true;
}

void D3D12Renderer::createFence()
{
	// Create synchronization objects and wait until assets have been uploaded to the GPU.
	if (FAILED(m_pD3DDevice->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&m_pFence))))
	{
		__debugbreak();
	}

	// Create an event handle to use for frame synchronization.
	m_hFenceEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);
}

void D3D12Renderer::cleanupFence()
{
	SAFE_CLOSE_HANDLE(m_hFenceEvent);
	SAFE_RELEASE(m_pFence);
}

uint64_t D3D12Renderer::fence()
{
	m_ui64FenceVaule++;
	m_pCommandQueue->Signal(m_pFence, m_ui64FenceVaule);
	m_pui64LastFenceValue[m_CurrContextIndex] = m_ui64FenceVaule;
	return m_ui64FenceVaule;
}

void D3D12Renderer::waitForFenceValue(uint64_t expectedFenceValue)
{
	// Wait until the previous frame is finished.
	if (m_pFence->GetCompletedValue() < expectedFenceValue)
	{
		m_pFence->SetEventOnCompletion(expectedFenceValue, m_hFenceEvent);
		WaitForSingleObject(m_hFenceEvent, INFINITE);
	}
}

bool D3D12Renderer::createDescriptorHeapForRTV()
{
	// Describe and create a render target view (RTV) descriptor heap.
	D3D12_DESCRIPTOR_HEAP_DESC rtvHeapDesc = {};
	rtvHeapDesc.NumDescriptors = SWAP_CHAIN_FRAME_COUNT;	// SwapChain Buffer 0	| SwapChain Buffer 1
	rtvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
	rtvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
	if (FAILED(m_pD3DDevice->CreateDescriptorHeap(&rtvHeapDesc, IID_PPV_ARGS(&m_pRTVHeap))))
	{
		__debugbreak();
	}

	m_rtvDescriptorSize = m_pD3DDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
	return true;
}

bool D3D12Renderer::createDescriptorHeapForDSV()
{
	// Describe and create a depth stencil view (DSV) descriptor heap.
	D3D12_DESCRIPTOR_HEAP_DESC dsvHeapDesc = {};
	dsvHeapDesc.NumDescriptors = 1;	// Default Depth Buffer
	dsvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
	dsvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
	if (FAILED(m_pD3DDevice->CreateDescriptorHeap(&dsvHeapDesc, IID_PPV_ARGS(&m_pDSVHeap))))
	{
		__debugbreak();
	}

	m_dsvDescriptorSize = m_pD3DDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_DSV);
	return true;
}

void D3D12Renderer::cleanupDescriptorHeapForRTV()
{
	SAFE_RELEASE(m_pRTVHeap);
}


void D3D12Renderer::cleanupDescriptorHeapForDSV()
{
	SAFE_RELEASE(m_pDSVHeap);
}

bool D3D12Renderer::initRenderThreadPool(int numThreads)
{
	m_pThreadDescList = new RenderThreadDesc[numThreads];
	memset(m_pThreadDescList, 0, sizeof(RenderThreadDesc) * numThreads);

	m_hCompleteEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);
	for (int i = 0; i < numThreads; i++)
	{
		for (int j = 0; j < RENDER_THREAD_EVENT_TYPE_COUNT; j++)
		{
			m_pThreadDescList[i].hEventList[j] = CreateEvent(nullptr, FALSE, FALSE, nullptr);

		}
		m_pThreadDescList[i].pRenderer = this;
		m_pThreadDescList[i].ThreadIndex = i;
		uint uiThreadID = 0;
		m_pThreadDescList[i].hThread = (HANDLE)_beginthreadex(nullptr, 0, RenderThread, m_pThreadDescList + i, 0, &uiThreadID);
	}
	return true;
}

void D3D12Renderer::cleanupRenderThreadPool()
{
	if (m_pThreadDescList)
	{
		for (int i = 0; i < m_NumRenderThreads; i++)
		{
			SetEvent(m_pThreadDescList[i].hEventList[RENDER_THREAD_EVENT_TYPE_DESTROY]);
			WaitForSingleObject(m_pThreadDescList[i].hThread, INFINITE);

			for (int j = 0; j < RENDER_THREAD_EVENT_TYPE_COUNT; j++)
			{
				SAFE_CLOSE_HANDLE(m_pThreadDescList[i].hEventList[j]);
			}
			SAFE_CLOSE_HANDLE(m_pThreadDescList[i].hThread);
		}

		SAFE_DELETE_ARRAY(m_pThreadDescList);
	}
	SAFE_CLOSE_HANDLE(m_hCompleteEvent);
}