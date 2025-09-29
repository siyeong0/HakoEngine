#include "pch.h"
#include <process.h>
#include <cmath>
#include "Common/ProcessorInfo.h"
#include "BasicMeshObject.h"
#include "SpriteObject.h"
#include "D3D12ResourceManager.h"
#include "FontManager.h"
#include "DescriptorPool.h"
#include "SimpleConstantBufferPool.h"
#include "SingleDescriptorAllocator.h"
#include "ShaderManager.h"
#include "SkyObject.h"
#include "ConstantBufferManager.h"
#include "TextureManager.h"
#include "RenderQueue.h"
#include "CommandListPool.h"
#include "RenderThread.h"
#include "D3D12Renderer.h"

using namespace DirectX;

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

bool ENGINECALL D3D12Renderer::Initialize(HWND hWnd, bool bEnableDebugLayer, bool bEnableGBV, bool bUseGpuUploadHeaps, const WCHAR* wchShaderPath)
{
	HRESULT hr = S_OK;
	ID3D12Debug* pDebugController = nullptr;
	IDXGIFactory4* pFactory = nullptr;
	IDXGIAdapter1* pAdapter = nullptr;
	DXGI_ADAPTER_DESC1 AdapterDesc = {};

	auto cleanupResources = [&]() {
		if (pDebugController)
		{
			pDebugController->Release();
			pDebugController = nullptr;
		}
		if (pAdapter)
		{
			pAdapter->Release();
			pAdapter = nullptr;
		}
		if (pFactory)
		{
			pFactory->Release();
			pFactory = nullptr;
		}
		};

	UINT createFactoryFlags = 0;
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
				pDebugController5->Release();
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
	UINT featureLevelNum = _countof(featureLevels);
	for (UINT featerLevelIndex = 0; featerLevelIndex < featureLevelNum; featerLevelIndex++)
	{
		UINT adapterIndex = 0;
		while (DXGI_ERROR_NOT_FOUND != pFactory->EnumAdapters1(adapterIndex, &pAdapter))
		{
			pAdapter->GetDesc1(&AdapterDesc);
			if (SUCCEEDED(D3D12CreateDevice(pAdapter, featureLevels[featerLevelIndex], IID_PPV_ARGS(&m_pD3DDevice))))
			{
				goto lb_exit;
			}
			pAdapter->Release();
			pAdapter = nullptr;
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
		SetDebugLayerInfo(m_pD3DDevice);
	}

	if (bUseGpuUploadHeaps)
	{
		m_bGpuUploadHeapsEnabled = CheckSupportGpuUploadHeap(m_pD3DDevice);
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
	UINT windowWidth = rect.right - rect.left;
	UINT windowHeight = rect.bottom - rect.top;
	UINT backBufferWidth = rect.right - rect.left;
	UINT backBufferHeight = rect.bottom - rect.top;

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
		pSwapChain1->Release();
		pSwapChain1 = nullptr;
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
	bInited = m_pShaderManager->Initialize(this, wchShaderPath, bEnableDebugLayer); // TODO: Use "bDebugShader" parameter.
	ASSERT(bInited, "ShaderManager initialization failed.");

	DWORD numPhysicalCores = 0;
	DWORD numLogicalCores = 0;
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
		m_ppRenderQueue[i] = new RenderQueue;
		m_ppRenderQueue[i]->Initialize(this, 8192);
	}

	m_pSkyObject = new SkyObject;
	m_pSkyObject->Initialize(this);

	initCamera();

	wcscpy_s(m_wchShaderPath, wchShaderPath);

	cleanupResources();
	return true;
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

#ifdef USE_MULTI_THREAD
	m_lActiveThreadCount = m_NumRenderThreads;
	for (int i = 0; i < m_NumRenderThreads; i++)
	{
		SetEvent(m_pThreadDescList[i].hEventList[RENDER_THREAD_EVENT_TYPE_PROCESS]);
	}
	WaitForSingleObject(m_hCompleteEvent, INFINITE);
#else
	// Each CommandList processes 400 items.
	for (int i = 0; i < m_NumRenderThreads; i++)
	{
		m_ppRenderQueue[i]->Process(i, pCommandListPool, m_pCommandQueue, 400, rtvHandle, dsvHandle, &m_Viewport, &m_ScissorRect);
	}
#endif	

	// Present
	ID3D12GraphicsCommandList6* pCommandList = pCommandListPool->GetCurrentCommandList();
	pCommandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(m_pRenderTargets[m_uiRenderTargetIndex], D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT));

	pCommandListPool->CloseAndExecute(m_pCommandQueue);

	// Reset all render queues
	for (int i = 0; i < m_NumRenderThreads; i++)
	{
		m_ppRenderQueue[i]->Reset();
	}
}

void ENGINECALL D3D12Renderer::Present()
{
	fence();
	// Transfer the Back Buffer to the Primary Buffer.

	UINT m_SyncInterval = 1;	// VSync On
	//UINT m_SyncInterval = 0;	// VSync Off

	UINT uiSyncInterval = m_SyncInterval;
	UINT uiPresentFlags = 0;

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
		if (m_ppRenderQueue[i])
		{
			delete m_ppRenderQueue[i];
			m_ppRenderQueue[i] = nullptr;
		}
	}
	for (int i = 0; i < MAX_PENDING_FRAME_COUNT; i++)
	{
		for (int j = 0; j < m_NumRenderThreads; j++)
		{
			if (m_ppCommandListPool[i][j])
			{
				delete m_ppCommandListPool[i][j];
				m_ppCommandListPool[i][j] = nullptr;
			}
			if (m_ppConstBufferManager[i][j])
			{
				delete m_ppConstBufferManager[i][j];
				m_ppConstBufferManager[i][j] = nullptr;
			}
			if (m_ppDescriptorPool[i][j])
			{
				delete m_ppDescriptorPool[i][j];
				m_ppDescriptorPool[i][j] = nullptr;
			}
		}
	}
	if (m_pTextureManager)
	{
		delete m_pTextureManager;
		m_pTextureManager = nullptr;
	}
	if (m_pResourceManager)
	{
		delete m_pResourceManager;
		m_pResourceManager = nullptr;
	}
	if (m_pFontManager)
	{
		delete m_pFontManager;
		m_pFontManager = nullptr;
	}
	if (m_pShaderManager)
	{
		delete m_pShaderManager;
		m_pShaderManager = nullptr;
	}

	if (m_pSingleDescriptorAllocator)
	{
		delete m_pSingleDescriptorAllocator;
		m_pSingleDescriptorAllocator = nullptr;
	}

	cleanupDescriptorHeapForRTV();
	cleanupDescriptorHeapForDSV();

	for (int i = 0; i < SWAP_CHAIN_FRAME_COUNT; i++)
	{
		if (m_pRenderTargets[i])
		{
			m_pRenderTargets[i]->Release();
			m_pRenderTargets[i] = nullptr;
		}
	}
	if (m_pDepthStencil)
	{
		m_pDepthStencil->Release();
		m_pDepthStencil = nullptr;
	}
	if (m_pSwapChain)
	{
		m_pSwapChain->Release();
		m_pSwapChain = nullptr;
	}

	if (m_pCommandQueue)
	{
		m_pCommandQueue->Release();
		m_pCommandQueue = nullptr;
	}

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

bool ENGINECALL D3D12Renderer::UpdateWindowSize(uint32_t backBufferWidth, uint32_t backBufferHeight)
{
	if ((backBufferWidth == 0 || backBufferHeight == 0 ) ||				// Zero size can be given when the window is minimized.
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
		m_pRenderTargets[n]->Release();
		m_pRenderTargets[n] = nullptr;
	}

	if (m_pDepthStencil)
	{
		m_pDepthStencil->Release();
		m_pDepthStencil = nullptr;
	}

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

	initCamera();

	return true;
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

void* ENGINECALL D3D12Renderer::CreateTiledTexture(UINT texWidth, UINT texHeight, uint8_t r, uint8_t g, uint8_t b)
{
	ASSERT(m_pTextureManager, "Texture manager is not initialized.");
	ASSERT(texWidth > 0 && texHeight > 0, "Invalid parameters.");

	DXGI_FORMAT texFormat = DXGI_FORMAT_R8G8B8A8_UNORM;

	uint8_t* image = (uint8_t*)malloc(texWidth * texHeight * 4);
	memset(image, 0, (size_t)texWidth * texHeight * 4);

	const RGBA WHITE = { 255, 255, 255, 255 };
	const RGBA BLACK = { 0, 0, 0, 255 };
	for (UINT y = 0; y < texHeight; y++)
	{
		for (UINT x = 0; x < texWidth; x++)
		{
			RGBA* pDest = reinterpret_cast<RGBA*>(image + (x + y * texWidth) * 4);
			*pDest = (((x ^ y) & 1) == 0) ? WHITE : BLACK;
		}
	}
	TextureHandle* pTexHandle = m_pTextureManager->CreateImmutableTexture(texWidth, texHeight, texFormat, image);

	free(image);
	image = nullptr;

	return pTexHandle;
}

void* ENGINECALL D3D12Renderer::CreateDynamicTexture(UINT texWidth, UINT texHeight)
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

void ENGINECALL D3D12Renderer::DeleteTexture(void* pTexHandle)
{
	// wait for all commands
	for (UINT i = 0; i < MAX_PENDING_FRAME_COUNT; i++)
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

bool ENGINECALL D3D12Renderer::WriteTextToBitmap(uint8_t* dstImage, UINT dstWidth, UINT dstHeight, UINT dstPitch, int* outWidth, int* outHeight, void* pFontObjHandle, const WCHAR* wchString, UINT len)
{
	bool bResult = m_pFontManager->WriteTextToBitmap(dstImage, dstWidth, dstHeight, dstPitch, outWidth, outHeight, (FontHandle*)pFontObjHandle, wchString, len);
	return bResult;
}

void ENGINECALL D3D12Renderer::RenderMeshObject(IMeshObject* pMeshObj, const XMMATRIX* pMatWorld)
{
	RenderItem item = {};
	item.Type = RENDER_ITEM_TYPE_MESH_OBJ;
	item.pObjHandle = pMeshObj;
	item.MeshObjParam.matWorld = *pMatWorld;

	bool bAdded = m_ppRenderQueue[m_CurrThreadIndex]->Add(&item);
	ASSERT(bAdded, "Render Queue is full.");

	m_CurrThreadIndex++;
	m_CurrThreadIndex = m_CurrThreadIndex % m_NumRenderThreads;
}

void ENGINECALL D3D12Renderer::RenderSpriteWithTex(void* pSprObjHandle, int posX, int posY, float scaleX, float scaleY, const RECT* pRect, float z, void* pTexHandle)
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

	bool bAdded = m_ppRenderQueue[m_CurrThreadIndex]->Add(&item);
	ASSERT(bAdded, "Render Queue is full.");

	m_CurrThreadIndex++;
	m_CurrThreadIndex = m_CurrThreadIndex % m_NumRenderThreads;
}

// TODO: Support rotation. Get Transform info.
void ENGINECALL D3D12Renderer::RenderSprite(void* pSprObjHandle, int posX, int posY, float scaleX, float scaleY, float z)
{
	RenderItem item = {};
	item.Type = RENDER_ITEM_TYPE_SPRITE;
	item.pObjHandle = pSprObjHandle;
	item.SpriteParam.PosX = posX;
	item.SpriteParam.PosY = posY;
	item.SpriteParam.ScaleX = scaleX;
	item.SpriteParam.ScaleY = scaleY;
	item.SpriteParam.bUseRect = FALSE;
	item.SpriteParam.Rect = {};
	item.SpriteParam.pTexHandle = nullptr;
	item.SpriteParam.Z = z;

	bool bAdded = m_ppRenderQueue[m_CurrThreadIndex]->Add(&item);
	ASSERT(bAdded, "Render Queue is full.");

	m_CurrThreadIndex++;
	m_CurrThreadIndex = m_CurrThreadIndex % m_NumRenderThreads;
}

void ENGINECALL D3D12Renderer::UpdateTextureWithImage(void* pTexHandle, const BYTE* pSrcBits, UINT srcWidth, UINT srcHeight)
{
	TextureHandle* pTextureHandle = (TextureHandle*)pTexHandle;
	ID3D12Resource* pDestTexResource = pTextureHandle->pTexResource;
	ID3D12Resource* pUploadBuffer = pTextureHandle->pUploadBuffer;

	D3D12_RESOURCE_DESC Desc = pDestTexResource->GetDesc();
	ASSERT(srcWidth <= Desc.Width, "Source width is too large for the destination texture.");
	ASSERT(srcHeight <= Desc.Height, "Source width is too large for the destination texture.");

	D3D12_PLACED_SUBRESOURCE_FOOTPRINT Footprint;
	UINT	Rows = 0;
	UINT64	RowSize = 0;
	UINT64	TotalBytes = 0;

	m_pD3DDevice->GetCopyableFootprints(&Desc, 0, 1, 0, &Footprint, &Rows, &RowSize, &TotalBytes);

	BYTE* pMappedPtr = nullptr;
	CD3DX12_RANGE readRange(0, 0);

	HRESULT hr = pUploadBuffer->Map(0, &readRange, reinterpret_cast<void**>(&pMappedPtr));
	ASSERT(SUCCEEDED(hr), "Failed to map the upload buffer.");

	const BYTE* pSrc = pSrcBits;
	BYTE* pDest = pMappedPtr;
	for (UINT y = 0; y < srcHeight; y++)
	{
		memcpy(pDest, pSrc, (size_t)srcWidth * 4);
		pSrc += (srcWidth * 4);
		pDest += Footprint.Footprint.RowPitch;
	}
	// Unmap
	pUploadBuffer->Unmap(0, nullptr);

	pTextureHandle->bUpdated = true;
}

void ENGINECALL D3D12Renderer::SetCameraPos(float x, float y, float z)
{
	m_CamPos = XMVectorSet(x, y, z, 1.0f);
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
	XMVECTOR CamMoveForward = XMVectorScale(m_CamDir, z);
	XMVECTOR CamMoveRight = XMVectorScale(m_CamRight, x);
	XMVECTOR CamMoveUp = XMVectorScale(m_CamUp, y);

	m_CamPos = XMVectorAdd(m_CamPos, CamMoveForward);
	m_CamPos = XMVectorAdd(m_CamPos, CamMoveRight);
	m_CamPos = XMVectorAdd(m_CamPos, CamMoveUp);
	m_CamPos.m128_f32[3] = 1.0f;

	updateCamera();
}

void ENGINECALL D3D12Renderer::GetCameraPos(float* outX, float* outY, float* outZ)
{
	*outX = m_CamPos.m128_f32[0];
	*outY = m_CamPos.m128_f32[1];
	*outZ = m_CamPos.m128_f32[2];
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

void D3D12Renderer::EnsureCompleted()
{
	// Wait for all commands.
	for (int i = 0; i < MAX_PENDING_FRAME_COUNT; i++)
	{
		waitForFenceValue(m_pui64LastFenceValue[i]);
	}
}

SimpleConstantBufferPool* D3D12Renderer::GetConstantBufferPool(EConstantBufferType type, int threadIndex) const
{
	ConstantBufferManager* pConstBufferManager = m_ppConstBufferManager[m_CurrContextIndex][threadIndex];
	SimpleConstantBufferPool* pConstBufferPool = pConstBufferManager->GetConstantBufferPool(type);
	return pConstBufferPool;
}

void D3D12Renderer::GetViewProjMatrix(XMMATRIX* outMatView, XMMATRIX* outMatProj) const
{
	*outMatView = XMMatrixTranspose(m_ViewMatrix);
	*outMatProj = XMMatrixTranspose(m_ProjMatrix);
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

void D3D12Renderer::ProcessByThread(int threadIndex)
{
	CommandListPool* pCommandListPool = m_ppCommandListPool[m_CurrContextIndex][threadIndex];	// 현재 사용중인 command list pool

	// Set RenderTarget to process the rendering queue.
	CD3DX12_CPU_DESCRIPTOR_HANDLE rtvHandle(m_pRTVHeap->GetCPUDescriptorHandleForHeapStart(), m_uiRenderTargetIndex, m_rtvDescriptorSize);
	CD3DX12_CPU_DESCRIPTOR_HANDLE dsvHandle(m_pDSVHeap->GetCPUDescriptorHandleForHeapStart());

	// Each CommandList processes 400 items.
	m_ppRenderQueue[threadIndex]->Process(threadIndex, pCommandListPool, m_pCommandQueue, 400, rtvHandle, dsvHandle, &m_Viewport, &m_ScissorRect);

	LONG currCount = _InterlockedDecrement(&m_lActiveThreadCount);
	if (0 == currCount)
	{
		SetEvent(m_hCompleteEvent);
	}
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
	XMVECTOR yAxis = XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f);
	XMVECTOR zAxis = XMVectorSet(0.0f, 0.0f, 1.0f, 0.0f);
	XMVECTOR xAxis = XMVectorSet(1.0f, 0.0f, 0.0f, 0.0f);

	XMMATRIX matRotPitch = XMMatrixRotationX(m_fCamPitch);
	XMMATRIX matRotYaw = XMMatrixRotationY(m_fCamYaw);


	XMMATRIX matCamRot = XMMatrixMultiply(matRotPitch, matRotYaw);

	m_CamDir = XMVector3Transform(zAxis, matCamRot);
	m_CamRight = XMVector3Cross(yAxis, m_CamDir);
	m_CamUp = XMVector3Cross(m_CamDir, m_CamRight);

	// view matrix
	m_ViewMatrix = XMMatrixLookToLH(m_CamPos, m_CamDir, m_CamUp);

	// 시야각 (FOV) 설정 (라디안 단위)
	float fovY = XM_PIDIV4; // 90도 (라디안으로 변환)

	// projection matrix
	float fAspectRatio = (float)m_Width / (float)m_Height;
	float fNear = 0.1f;
	float fFar = 1000.0f;
	m_ProjMatrix = XMMatrixPerspectiveFovLH(fovY, fAspectRatio, fNear, fFar);

	XMVECTOR determinant;
	m_InvViewMatrix = XMMatrixInverse(&determinant, m_ViewMatrix);
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
	if (m_pRTVHeap)
	{
		m_pRTVHeap->Release();
		m_pRTVHeap = nullptr;
	}
}


void D3D12Renderer::cleanupDescriptorHeapForDSV()
{
	if (m_pDSVHeap)
	{
		m_pDSVHeap->Release();
		m_pDSVHeap = nullptr;
	}
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
		UINT uiThreadID = 0;
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
			CloseHandle(m_pThreadDescList[i].hThread);
			m_pThreadDescList[i].hThread = nullptr;

			for (int j = 0; j < RENDER_THREAD_EVENT_TYPE_COUNT; j++)
			{
				CloseHandle(m_pThreadDescList[i].hEventList[j]);
				m_pThreadDescList[i].hEventList[j] = nullptr;
			}
		}

		delete[] m_pThreadDescList;
		m_pThreadDescList = nullptr;
	}
	if (m_hCompleteEvent)
	{
		CloseHandle(m_hCompleteEvent);
		m_hCompleteEvent = nullptr;
	}
}