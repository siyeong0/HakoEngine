#pragma once

class ShaderTable;
class CIndexCreator;
class D3D12ResourceRecycleBin;
class D3D12Renderer;

class RayTracingManager
{
public:
	RayTracingManager() = default;
	~RayTracingManager() { Cleanup(); }

	bool Initialize(D3D12Renderer* pRenderer, uint width, uint height, uint maxNumBLASs = 10000);
	void Cleanup();

	void DoRaytracing(ID3D12GraphicsCommandList6* pCommandList);
	BLASHandle* AllocBLAS(ID3D12Resource* pVertexBuffer, uint vertexSize, uint numVertices, const IndexedTriGroup* pTriGroupInfoList, uint numTriGroupInfos, bool bAllowUpdate);
	void FreeBLAS(BLASHandle* pBLASHandle);
	bool UpdateAccelerationStructure(ID3D12GraphicsCommandList6* pCommandList);
	void UpdateManagedResource();
	void UpdateBLAS(int threadIndex, ID3D12GraphicsCommandList6* pCommandList, BLASHandle* pBLASHandle, const Matrix4x4& worldMatrix);
	void UpdateWindowSize(uint width, uint height);

	ID3D12Resource* GetOutputResource() { return m_pOutputDiffuse; }
	ID3D12Resource* GetDepthResource() { return m_pOutputDepth; }
	bool IsUpdatedAccelerationStructure() const { return (BOOL)(m_UpdateAccelerationStructureFlags != 0); }

	uint GetMaxRadianceRecursionDepth() const { return MAX_RECURSION_DEPTH; }
	uint GetMaxShadowRecursionDepth() const { return MAX_SHADOW_RECURSION_DEPTH; }

private:
	bool buildBLAS(ID3D12GraphicsCommandList6* pCommandList, BLASHandle* pBLASHandle);
	ID3D12Resource* buildTLAS(ID3D12GraphicsCommandList6* pCommandList, bool bAllowUpdate, uint currContextIndex);

	void updateHitGroupShaderTable(uint numShaderRecords);

	bool createOutputDiffuseBuffer(uint width, uint height);
	void cleanupOutputDiffuseBuffer();
	bool createOutputDepthBuffer(uint width, uint height);
	void cleanupOutputDepthBuffer();

	void createRootSignatures();
	void createRaytracingPipelineStateObject();

	void buildShaderTables();
	void cleanupShaderTables();

	void createDescriptorHeapCBV_SRV_UAV();
	void cleanupDescriptorHeapCBV_SRV_UAV();

	void createShaderVisibleHeap(uint maxNumDescriptors);
	void cleanupDispatchHeap();

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
		DISPATCH_DESCRIPTOR_INDEX_ATMOSPHERE_CBV,
		DISPATCH_DESCRIPTOR_INDEX_OUTPUT_DIFFUSE,
		DISPATCH_DESCRIPTOR_INDEX_OUTPUT_DEPTH,
		DISPATCH_DESCRIPTOR_INDEX_SKY_TRANSMITTANCE,
		DISPATCH_DESCRIPTOR_INDEX_SKY_SCATTERING,
		DISPATCH_DESCRIPTOR_INDEX_SKY_IRRADIANCE,
		DISPATCH_DESCRIPTOR_INDEX_COUNT,
	};
	enum LOCAL_ROOT_PARAM_DESCRIPTOR_INDEX
	{
		LOCAL_ROOT_PARAM_DESCRIPTOR_INDEX_VB,
		LOCAL_ROOT_PARAM_DESCRIPTOR_INDEX_IB,
		LOCAL_ROOT_PARAM_DESCRIPTOR_INDEX_DIFFUSE,
		LOCAL_ROOT_PARAM_DESCRIPTOR_INDEX_NORMALL,
		LOCAL_ROOT_PARAM_DESCRIPTOR_COUNT,
	};
	enum UPDATE_ACCELERATION_STRCTURE_TYPE
	{
		UPDATE_ACCELERATION_STRCTURE_TYPE_HIT_GROUP_SHADER_TABLE = 0b01,
		UPDATE_ACCELERATION_STRCTURE_TYPE_TLAS = 0b10
	};
	static const uint MAX_RECURSION_DEPTH = 3;
	static const uint MAX_RADIANCE_RECURSION_DEPTH = std::min(MAX_RECURSION_DEPTH, 3u);
	static const uint MAX_SHADOW_RECURSION_DEPTH = std::min(MAX_RECURSION_DEPTH, 2u);

	D3D12Renderer* m_pRenderer = nullptr;
	ID3D12Device5* m_pD3DDevice = nullptr;

	D3D12ResourceRecycleBin* m_pResourceBinTLAS = nullptr;
	D3D12ResourceRecycleBin* m_pResourceBinBLAS = nullptr;
	D3D12ResourceRecycleBin* m_pResourceBinScratchResource = nullptr;
	D3D12ResourceRecycleBin* m_pResourceBinTLASInstanceDescList = nullptr;

	CIndexCreator* m_pIndexCreator = nullptr;

	ID3D12Resource* m_pOutputDiffuse = nullptr;	// raytracing output - diffuse
	ID3D12Resource* m_pOutputDepth = nullptr;	// raytracing output - depth	
	uint m_Width = 0;
	uint m_Height = 0;

	ShaderHandle* m_pRayShader = nullptr;
	ID3D12RootSignature* m_pRaytracingGlobalRootSignature = nullptr;
	ID3D12RootSignature* m_pRaytracingLocalRootSignature = nullptr;
	ID3D12StateObject* m_pDXRStateObject = nullptr;

	ID3D12DescriptorHeap* m_pCommonDescriptorHeap = nullptr;
	ID3D12DescriptorHeap* m_pShaderVisibleDescriptorHeap = nullptr;	// ID에 따라 srv,uav 위치 고정.
	uint m_MaxNumShaderVisibleHeapDescriptors = 0;
	uint m_DescriptorSize = 0;

	ShaderTable* m_pRayGenShaderTable = nullptr;
	ShaderTable* m_pMissShaderTable = nullptr;
	ShaderTable* m_pHitGroupShaderTable = nullptr;
	uint m_MissShaderTableStrideInBytes = 0;
	uint m_HitGroupShaderTableStrideInBytes = 0;
	uint m_HitGroupShaderRecordNum = 0;
	uint m_ShaderIdentifierSize = D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES;
	uint m_HitGroupShaderRecordSize = 0;

	uint m_MaxNumBLASs = 0;
	std::list<BLASHandle*> m_GlobalBLASHandleList; // BLAS Instance list
	std::vector<BLASInstance> m_BLASInstanceListThisFrame[MAX_RENDER_THREAD_COUNT]; // for TLAS build

	ID3D12Resource* m_pBLASInstanceDescResouce = nullptr;
	ID3D12Resource* m_pTLAS = nullptr;

	uint m_UpdateAccelerationStructureFlags = 0;
};