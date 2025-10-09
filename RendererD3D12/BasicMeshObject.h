#pragma once

enum EBasicMeshDescriptorIndexPerObj
{
	BASIC_MESH_DESCRIPTOR_INDEX_PER_OBJ_CBV = 0
};
enum EBasicMeshDescriptorIndexPerTriGroup
{
	BASIC_MESH_DESCRIPTOR_INDEX_PER_TRI_GROUP_TEX = 0
};

struct PSOHandle;
class D3D12Renderer;

class BasicMeshObject : public IMeshObject
{
public:
	static constexpr uint DESCRIPTOR_COUNT_PER_OBJ = 1;			// | Constant Buffer
	static constexpr uint DESCRIPTOR_COUNT_PER_TRI_GROUP = 1;	// | SRV(tex)
	static constexpr uint MAX_TRI_GROUP_COUNT_PER_OBJ = 8;
	static constexpr uint MAX_DESCRIPTOR_COUNT_FOR_DRAW = DESCRIPTOR_COUNT_PER_OBJ + (MAX_TRI_GROUP_COUNT_PER_OBJ * DESCRIPTOR_COUNT_PER_TRI_GROUP);

public:
	// Derived from IUnknown
	STDMETHODIMP			QueryInterface(REFIID, void** ppv);
	STDMETHODIMP_(ULONG)	AddRef();
	STDMETHODIMP_(ULONG)	Release();

	// Derived from IMeshObject
	bool ENGINECALL BeginCreateMesh(const Vertex* vertices, uint numVertices, uint numTriGroups);
	bool ENGINECALL InsertTriGroup(const uint16_t* indices, uint numTriangles, const WCHAR* wchTexFileName);
	void ENGINECALL EndCreateMesh();

	// Internal
	BasicMeshObject() = default;
	~BasicMeshObject() { cleanup(); }

	bool Initialize(D3D12Renderer* pRenderer);
	void Draw(int threadIndex, ID3D12GraphicsCommandList6* pCommandList, const Matrix4x4* worldMatrix);
	void UpdateBLASTransform(const Matrix4x4& worldMatrix);

private:
	bool initPipelineState();

	void deleteTriGroup(IndexedTriGroup* pTriGroup);
	void cleanup();

private:
	int m_RefCount = 1;
	D3D12Renderer* m_pRenderer = nullptr;

	ID3D12Resource* m_pVertexBuffer = nullptr;
	D3D12_VERTEX_BUFFER_VIEW m_VertexBufferView = {};

	PSOHandle* m_pPSOHandle = nullptr;

	IndexedTriGroup* m_pTriGroupList = nullptr;
	uint m_NumTriGroups = 0;
	uint m_MaxNumTriGroups = 0;

	BLASHandle* m_pBLASHandle = nullptr;
};


