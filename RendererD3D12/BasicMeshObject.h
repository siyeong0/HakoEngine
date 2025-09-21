#pragma once

enum EBasicMeshDescriptorIndexPerObj
{
	BASIC_MESH_DESCRIPTOR_INDEX_PER_OBJ_CBV = 0
};
enum EBasicMeshDescriptorIndexPerTriGroup
{
	BASIC_MESH_DESCRIPTOR_INDEX_PER_TRI_GROUP_TEX = 0
};

struct IndexedTriGroup
{
	ID3D12Resource* IndexBuffer = nullptr;
	D3D12_INDEX_BUFFER_VIEW IndexBufferView = {};
	DWORD NumTriangles = 0;
	TextureHandle* pTexHandle = nullptr;
};

class D3D12Renderer;

class BasicMeshObject : public IMeshObject
{
public:
	static constexpr UINT DESCRIPTOR_COUNT_PER_OBJ = 1;			// | Constant Buffer
	static constexpr UINT DESCRIPTOR_COUNT_PER_TRI_GROUP = 1;	// | SRV(tex)
	static constexpr UINT MAX_TRI_GROUP_COUNT_PER_OBJ = 8;
	static constexpr UINT MAX_DESCRIPTOR_COUNT_FOR_DRAW = DESCRIPTOR_COUNT_PER_OBJ + (MAX_TRI_GROUP_COUNT_PER_OBJ * DESCRIPTOR_COUNT_PER_TRI_GROUP);

public:
	// Derived from IUnknown
	STDMETHODIMP			QueryInterface(REFIID, void** ppv);
	STDMETHODIMP_(ULONG)	AddRef();
	STDMETHODIMP_(ULONG)	Release();

	// Derived from IMeshObject
	bool ENGINECALL BeginCreateMesh(const BasicVertex* vertices, uint32_t numVertices, uint32_t numTriGroups);
	bool ENGINECALL InsertTriGroup(const uint16_t* indices, uint32_t numTriangles, const WCHAR* wchTexFileName);
	void ENGINECALL EndCreateMesh();

	// Internal
	BasicMeshObject() = default;
	~BasicMeshObject() { cleanup(); }

	bool Initialize(D3D12Renderer* pRenderer);
	void Draw(int threadIndex, ID3D12GraphicsCommandList* pCommandList, const XMMATRIX* worlMatrix);

private:
	bool initCommonResources();
	void cleanupSharedResources();

	bool initRootSinagture();
	bool initPipelineState();

	void deleteTriGroup(IndexedTriGroup* pTriGroup);
	void cleanup();

private:
	// Shared by all CBasicMeshObject instances.
	static ID3D12RootSignature* m_pRootSignature;
	static ID3D12PipelineState* m_pPipelineState;
	static int m_InitRefCount;

	int m_RefCount = 1;
	D3D12Renderer* m_pRenderer = nullptr;

	ID3D12Resource* m_pVertexBuffer = nullptr;
	D3D12_VERTEX_BUFFER_VIEW m_VertexBufferView = {};

	IndexedTriGroup* m_pTriGroupList = nullptr;
	UINT m_NumTriGroups = 0;
	UINT m_MaxNumTriGroups = 0;
};


