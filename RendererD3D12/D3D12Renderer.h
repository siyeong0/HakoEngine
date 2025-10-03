#pragma once
#include "Common/Common.h"
#include "Interface/IRenderer.h"
#include "Renderer_typedef.h"

#define USE_MULTI_THREAD

class ConstantBufferManager;
class CommandListPool;
class D3D12ResourceManager;
class DescriptorPool;
class FontManager;
class PSOManager;
class RootSignatureManager;
class RenderQueue;
struct RenderThreadDesc;
class SimpleConstantBufferPool;
class SingleDescriptorAllocator;
class SkyObject;
class ShaderManager;
class TextureManager;

class D3D12Renderer : public IRenderer
{
public:
	// Derived from IUnknown
	STDMETHODIMP			QueryInterface(REFIID, void** ppv) override;
	STDMETHODIMP_(ULONG)	AddRef() override;
	STDMETHODIMP_(ULONG)	Release() override;

	// Derived from IRenderer
	bool ENGINECALL Initialize(HWND hWnd, bool bEnableDebugLayer, bool bEnableGBV, bool bEnableShaderDebug, bool bUseGpuUploadHeaps, const WCHAR* wchShaderPath) override;
	void ENGINECALL Cleanup() override;
	void ENGINECALL Update(float dt) override;
	void ENGINECALL BeginRender() override;
	void ENGINECALL EndRender() override;
	void ENGINECALL Present() override;

	void ENGINECALL RenderMeshObject(IMeshObject* pMeshObj, const XMMATRIX* pMatWorld, ERenderPassType renderPass) override;
	void ENGINECALL RenderSpriteWithTex(void* pSprObjHandle, int posX, int posY, float scaleX, float scaleY, const RECT* pRect, float z, void* pTexHandle, ERenderPassType renderPass) override;
	void ENGINECALL RenderSprite(void* pSprObjHandle, int posX, int posY, float scaleX, float scaleY, float z, ERenderPassType renderPass) override;

	IMeshObject* ENGINECALL CreateBasicMeshObject() override;
	ISprite* ENGINECALL CreateSpriteObject() override;
	ISprite* ENGINECALL CreateSpriteObject(const WCHAR* wchTexFileName) override;
	ISprite* ENGINECALL CreateSpriteObject(const WCHAR* wchTexFileName, int posX, int posY, int width, int height) override;

	void* ENGINECALL CreateTiledTexture(UINT texWidth, UINT texHeight, uint8_t r, uint8_t g, uint8_t b) override;
	void* ENGINECALL CreateDynamicTexture(UINT texWidth, UINT texHeight) override;
	void* ENGINECALL CreateTextureFromFile(const WCHAR* wchFileName) override;
	void ENGINECALL UpdateTextureWithImage(void* pTexHandle, const BYTE* pSrcBits, UINT srcWidth, UINT srcHeight) override;
	void ENGINECALL DeleteTexture(void* pTexHandle) override;

	void* ENGINECALL CreateFontObject(const WCHAR* wchFontFamilyName, float fontSize) override;
	void ENGINECALL DeleteFontObject(void* pFontHandle) override;
	bool ENGINECALL WriteTextToBitmap(uint8_t* dstImage, UINT dstWidth, UINT dstHeight, UINT dstPitch, int* outWidth, int* outHeight, void* pFontObjHandle, const WCHAR* wchString, UINT len) override;

	bool ENGINECALL UpdateWindowSize(uint32_t backBufferWidth, uint32_t backBufferHeight) override;
	void ENGINECALL SetCameraPos(float x, float y, float z) override;
	void ENGINECALL SetCameraRot(float yaw, float pitch, float roll) override;
	void ENGINECALL MoveCamera(float x, float y, float z) override;
	void ENGINECALL GetCameraPos(float* outX, float* outY, float* outZ) override;
	int	ENGINECALL GetCommandListCount() override;
	bool ENGINECALL IsGpuUploadHeapsEnabled() const override;

	// Internal
	D3D12Renderer() = default;
	~D3D12Renderer() { Cleanup(); };

	// from RenderThread
	void ProcessByThread(int threadIndex);
	void EnsureCompleted();

	ID3D12Device5* GetD3DDevice() const { return m_pD3DDevice; }
	D3D12ResourceManager* GetResourceManager() { return m_pResourceManager; }
	ShaderManager* GetShaderManager() { return m_pShaderManager; }
	RootSignatureManager* GetRootSignatureManager() { return m_pRootSignatureManager; }
	PSOManager* GetPSOManager() { return m_pPSOManager; }

	DescriptorPool* GetDescriptorPool(int threadIndex) const { return m_ppDescriptorPool[m_CurrContextIndex][threadIndex]; }
	SimpleConstantBufferPool* GetConstantBufferPool(EConstantBufferType type, int threadIndex) const;

	inline uint32_t GetSrvDescriptorSize() const { return m_srvDescriptorSize; }
	inline SingleDescriptorAllocator* GetSingleDescriptorAllocator() const { return m_pSingleDescriptorAllocator; }
	inline int GetScreenWidth() const { return m_Width; }
	inline int GetScreenHeight() const { return m_Height; }
	inline float GetDPI() const { return m_DPI; }
	inline bool IsGpuUploadHeapsEnabledInl() const { return m_bGpuUploadHeapsEnabled; }

	const CB_PerFrame& GetFrameCBData() { return m_PerFrameCB; };
	void GetViewProjMatrix(XMMATRIX* outMatView, XMMATRIX* outMatProj) const;

	void SetCurrentPathForShader() const;
	void RestoreCurrentPath() const;

private:
	void initCamera();
	void updateCamera();

	bool createDepthStencil(int width, int height);

	void createFence();
	void cleanupFence();
	uint64_t fence();
	void waitForFenceValue(uint64_t expectedFenceValue);

	bool createDescriptorHeapForRTV();
	bool createDescriptorHeapForDSV();
	void cleanupDescriptorHeapForRTV();
	void cleanupDescriptorHeapForDSV();

	// for multi-threads
	bool initRenderThreadPool(int numThreads);
	void cleanupRenderThreadPool();

private:
	static constexpr UINT MAX_DRAW_COUNT_PER_FRAME = 4096;
	static constexpr UINT MAX_DESCRIPTOR_COUNT = 4096;
	static constexpr UINT MAX_RENDER_THREAD_COUNT = 8;

	int	m_RefCount = 1;
	HWND m_hWnd = nullptr;
	ID3D12Device5* m_pD3DDevice = nullptr;
	ID3D12CommandQueue* m_pCommandQueue = nullptr;

	SingleDescriptorAllocator* m_pSingleDescriptorAllocator = nullptr;

	CommandListPool* m_ppCommandListPool[MAX_PENDING_FRAME_COUNT][MAX_RENDER_THREAD_COUNT] = {};
	DescriptorPool* m_ppDescriptorPool[MAX_PENDING_FRAME_COUNT][MAX_RENDER_THREAD_COUNT] = {};
	ConstantBufferManager* m_ppConstBufferManager[MAX_PENDING_FRAME_COUNT][MAX_RENDER_THREAD_COUNT] = {};

	static constexpr size_t NUM_RENDER_QUEUE_ITEMS_OPAQUE = 8192;
	static constexpr size_t NUM_RENDER_QUEUE_ITEMS_TRANSPARENT = 2048;

	std::atomic<ERenderPassType> m_RenderPhase = {};
	RenderQueue* m_ppRenderQueueOpaque[MAX_RENDER_THREAD_COUNT] = {};
	RenderQueue* m_ppRenderQueueTrasnparent[MAX_RENDER_THREAD_COUNT] = {};

	int m_NumRenderThreads = 0;
	int m_CurrThreadIndex = 0;

	LONG volatile m_lActiveThreadCount = 0;
	HANDLE m_hCompleteEvent = nullptr;
	RenderThreadDesc* m_pThreadDescList = nullptr;

	TextureManager* m_pTextureManager = nullptr;
	D3D12ResourceManager* m_pResourceManager = nullptr;
	FontManager* m_pFontManager = nullptr;
	ShaderManager* m_pShaderManager = nullptr;
	RootSignatureManager* m_pRootSignatureManager = nullptr;
	PSOManager* m_pPSOManager = nullptr;

	uint64_t m_pui64LastFenceValue[MAX_PENDING_FRAME_COUNT] = {};
	uint64_t m_ui64FenceVaule = 0;

	D3D_FEATURE_LEVEL m_FeatureLevel = D3D_FEATURE_LEVEL_11_0;
	DXGI_ADAPTER_DESC1 m_AdapterDesc = {};
	bool m_bGpuUploadHeapsEnabled = false;
	IDXGISwapChain3* m_pSwapChain = nullptr;
	D3D12_VIEWPORT m_Viewport = {};
	D3D12_RECT m_ScissorRect = {};
	int m_Width = -1;
	int m_Height = -1;
	float m_DPI = 96.0f;
	ID3D12Resource* m_pRenderTargets[SWAP_CHAIN_FRAME_COUNT] = {};
	ID3D12Resource* m_pDepthStencil = nullptr;
	ID3D12DescriptorHeap* m_pRTVHeap = nullptr;
	ID3D12DescriptorHeap* m_pDSVHeap = nullptr;
	ID3D12DescriptorHeap* m_pSRVHeap = nullptr;
	uint32_t m_rtvDescriptorSize = 0;
	uint32_t m_srvDescriptorSize = 0;
	uint32_t m_dsvDescriptorSize = 0;
	uint32_t m_SwapChainFlags = 0;
	int m_uiRenderTargetIndex = 0;
	HANDLE	m_hFenceEvent = nullptr;
	ID3D12Fence* m_pFence = nullptr;

	int	m_CurrContextIndex = 0;
	CB_PerFrame m_PerFrameCB = {};

	XMVECTOR m_CamPos = {};
	XMVECTOR m_CamDir = {};
	XMVECTOR m_CamRight = {};
	XMVECTOR m_CamUp = {};
	float m_fCamYaw = 0.0f;
	float m_fCamPitch = 0.0f;
	float m_fCamRoll = 0.0f;

	SkyObject* m_pSkyObject = {};

	WCHAR m_wchCurrentPathBackup[_MAX_PATH] = {};
	WCHAR m_wchShaderPath[_MAX_PATH] = {};
};

