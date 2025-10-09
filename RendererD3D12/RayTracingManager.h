#pragma once

class ShaderTable;
class D3D12Renderer;

class RayTracingManager
{
public:
	RayTracingManager() = default;
	~RayTracingManager() { Cleanup(); }

	bool Initialize(D3D12Renderer* pRenderer, UINT width, UINT height);
	void Cleanup();

	void DoRaytracing(ID3D12GraphicsCommandList6* pCommandList);
	BLASInstance* AllocBLAS(ID3D12Resource* pVertexBuffer, UINT vertexSize, UINT numVertices, const BLASBuilTriGroupInfo* pTriGroupInfoList, UINT numTriGroupInfos, bool bAllowUpdate);
	void FreeBLAS(BLASInstance* pBlasHandle);

	void UpdateWindowSize(UINT width, UINT height);

	ID3D12Resource* GetOutputResource() { return m_pOutputDiffuse; }
	ID3D12Resource* GetDepthResource() { return m_pOutputDepth; }

private:
	bool initAccelerationStructure();
	BLASInstance* buildBLAS(ID3D12Resource* pVertexBuffer, UINT vertexSize, UINT numVertices, const BLASBuilTriGroupInfo* pTriGroupInfoList, UINT numTriGroupInfos, bool bAllowUpdate);
	ID3D12Resource* buildTLAS(ID3D12Resource* pInstanceDescResource, BLASInstance** ppInstanceList, UINT numBlasInstances, bool bAllowUpdate, UINT currContextIndex);

	bool createOutputDiffuseBuffer(UINT width, UINT height);
	void cleanupOutputDiffuseBuffer();
	bool createOutputDepthBuffer(UINT width, UINT height);
	void cleanupOutputDepthBuffer();

	void createRootSignatures();
	void createRaytracingPipelineStateObject();

	void buildShaderTables();
	void cleanupShaderTables();

	void createDescriptorHeapCBV_SRV_UAV();
	void cleanupDescriptorHeapCBV_SRV_UAV();

	void createShaderVisibleHeap();
	void cleanupDispatchHeap();

	void createCommandList();
	void cleanupCommandList();
	void createFence();
	void cleanupFence();
	uint64_t fence();
	void waitForFenceValue();

	bool initMesh();
	void cleanupMesh();

private:
	enum COMMON_DESCRIPTOR_INDEX
	{
		COMMON_DESCRIPTOR_INDEX_OUTPUT_DIFFUSE_UAV,	// UAV - Output - Diffuse
		COMMON_DESCRIPTOR_INDEX_OUTPUT_DEPTH_UAV,	// UAV - Output - Depth
		COMMON_DESCRIPTOR_COUNT,
	};
	enum DISPATCH_DESCRIPTOR_INDEX
	{
		DISPATCH_DESCRIPTOR_INDEX_RAYTRACING_CBV,
		DISPATCH_DESCRIPTOR_INDEX_OUTPUT_DIFFUSE,
		DISPATCH_DESCRIPTOR_INDEX_OUTPUT_DEPTH,
		DISPATCH_DESCRIPTOR_INDEX_COUNT,
	};
	enum LOCAL_ROOT_PARAMETER_INDEX
	{
		LOCAL_ROOT_PARAMETER_INDEX_VB,
		LOCAL_ROOT_PARAMETER_INDEX_IB,
		LOCAL_ROOT_PARAMETER_INDEX_TEX,
		LOCAL_ROOT_PARAMETER_COUNT,
	};
	static const UINT MAX_RECURSION_DEPTH = 1;
	static const UINT MAX_RADIANCE_RECURSION_DEPTH = std::min<UINT>(MAX_RECURSION_DEPTH, 1u);


	D3D12Renderer* m_pRenderer = nullptr;
	ID3D12Device5* m_pD3DDevice = nullptr;
	ID3D12CommandQueue* m_pCommandQueue = nullptr;
	ID3D12CommandAllocator* m_pCommandAllocator = nullptr;
	ID3D12GraphicsCommandList6* m_pCommandList = nullptr;

	HANDLE m_hFenceEvent = nullptr;
	ID3D12Fence* m_pFence = nullptr;
	uint64_t m_ui64FenceValue = 0;

	ID3D12Resource* m_pOutputDiffuse = nullptr;	// raytracing output - diffuse
	ID3D12Resource* m_pOutputDepth = nullptr;	// raytracing output - depth	
	UINT m_Width = 0;
	UINT m_Height = 0;

	ShaderHandle* m_pRayShader = nullptr;
	ID3D12RootSignature* m_pRaytracingGlobalRootSignature = nullptr;
	ID3D12RootSignature* m_pRaytracingLocalRootSignature = nullptr;
	ID3D12StateObject* m_pDXRStateObject = nullptr;

	ID3D12DescriptorHeap* m_pCommonDescriptorHeap = nullptr;
	ID3D12DescriptorHeap* m_pShaderVisibleDescriptorHeap = nullptr;	// ID에 따라 srv,uav 위치 고정.
	UINT m_DescriptorSize = 0;

	ShaderTable* m_pRayGenShaderTable = nullptr;
	ShaderTable* m_pMissShaderTable = nullptr;
	ShaderTable* m_pHitGroupShaderTable = nullptr;
	UINT m_MissShaderTableStrideInBytes = 0;
	UINT m_HitGroupShaderTableStrideInBytes = 0;
	UINT m_HitGroupShaderRecordNum = 0;
	UINT m_ShaderIdentifierSize = D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES;

	std::list<BLASInstance*> m_BlasInstanceList; // BLAS Instance list

	ID3D12Resource* m_pBLASInstanceDescResouce = nullptr;
	ID3D12Resource* m_pTLAS = nullptr;
	BLASInstance* m_pBlasInstance = nullptr;

	// TODO: remove later
	// Mesh Data
	ID3D12Resource* m_pVertexBuffer = nullptr;	// vertex data
	ID3D12Resource* m_pIndexBuffer = nullptr;	// index data
	ID3D12Resource* m_pTexResource = nullptr;
	UINT	m_TexWidth = 0;
	UINT	m_TexHeight = 0;
	DXGI_FORMAT m_TexFormat = DXGI_FORMAT_R8G8B8A8_UNORM;
};