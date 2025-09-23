#pragma once

enum ESpriteDescriptorIndex
{
	SPRITE_DESCRIPTOR_INDEX_CBV = 0,
	SPRITE_DESCRIPTOR_INDEX_TEX = 1
};

class D3D12Renderer;

class SpriteObject : public ISprite
{
public:
	static constexpr UINT DESCRIPTOR_COUNT_FOR_DRAW = 2;	// | Constant Buffer | Tex |
public:
	// derived from IUnknown
	STDMETHODIMP			QueryInterface(REFIID, void** ppv);
	STDMETHODIMP_(ULONG)	AddRef();
	STDMETHODIMP_(ULONG)	Release();

	bool Initialize(D3D12Renderer* pRenderer);
	bool Initialize(D3D12Renderer* pRenderer, const WCHAR* wchTexFileName, const RECT* pRectOrNull);
	void DrawWithTex(int threadIndex, ID3D12GraphicsCommandList* pCommandList, const XMFLOAT2* pPos, const XMFLOAT2* pScale, const RECT* pRect, float Z, TextureHandle* pTexHandle);
	void Draw(int threadIndex, ID3D12GraphicsCommandList* pCommandList, const XMFLOAT2* pPos, const XMFLOAT2* pScale, float Z);

	SpriteObject();
	~SpriteObject();

private:
	bool initCommonResources();
	void cleanupSharedResources();

	bool initRootSinagture();
	bool initPipelineState();
	bool initMesh();

	void cleanup();

private:
	// shared by all CSpriteObject instances.
	static ID3D12RootSignature* m_pRootSignature;
	static ID3D12PipelineState* m_pPipelineState;
	static DWORD m_InitRefCount;

	// vertex data
	static ID3D12Resource* m_pVertexBuffer;
	static D3D12_VERTEX_BUFFER_VIEW m_VertexBufferView;

	// index data
	static ID3D12Resource* m_pIndexBuffer;
	static D3D12_INDEX_BUFFER_VIEW m_IndexBufferView;

	DWORD m_RefCount = 1;
	TextureHandle* m_pTexHandle = nullptr;
	D3D12Renderer* m_pRenderer = nullptr;
	RECT m_Rect = {};
	XMFLOAT2 m_Scale = { 1.0f, 1.0f };

	DWORD m_NumTriGroups = 0;
	DWORD m_MaxNumTriGroups = 0;
};

