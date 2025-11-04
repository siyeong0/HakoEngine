#include "pch.h"
#include "D3D12ResourceManager.h"
#include "D3D12ResourceRecycleBin.h"
#include "SimpleConstantBufferPool.h"
#include "ConstantBufferManager.h"
#include "ShaderManager.h"
#include "ShaderRecord.h"
#include "Generic/IndexCreator.h"
#include "D3D12Renderer.h"
#include "ShaderTable.h"
#include "RootSignatureManager.h"
#include "RayTracingManager.h"

constexpr uint NUM_RAYTRACING_SHADER_TYPES = 2;

// Shader
const wchar_t* g_RaygenShaderName = { L"MyRaygenShader_RadianceRay" };
const wchar_t* g_ClosestHitShaderNames[NUM_RAYTRACING_SHADER_TYPES] =
{
	L"MyClosestHitShader_RadianceRay",
	L"MyClosestHitShader_ShadowRay"
};
const wchar_t* g_MissShaderNames[NUM_RAYTRACING_SHADER_TYPES] =
{
	L"MyMissShader_RadianceRay" ,
	L"MyMissShader_ShadowRay"
};
const wchar_t* g_AnyHitShaderNames[NUM_RAYTRACING_SHADER_TYPES] =
{
	L"MyAnyHitShader_RadianceRay",
	L"MyAnyHitShader_ShadowRay"
};
// Procedural (sphere)
const wchar_t* g_IntersectionShaderName_Proc = L"MyIntersectionShader_Sphere";
const wchar_t* g_ClosestHitShaderNames_Proc[NUM_RAYTRACING_SHADER_TYPES] =
{
	L"MyClosestHitShader_RadianceRay_Proc",
	L"MyClosestHitShader_ShadowRay_Proc"
};
// Procedural (casper)
const wchar_t* g_IntersectionShaderName_Casper = L"MyIntersectionShader_Casper";
const wchar_t* g_ClosestHitShaderNames_Casper[NUM_RAYTRACING_SHADER_TYPES] =
{
	L"MyClosestHitShader_RadianceRay_Casper",
	L"MyClosestHitShader_ShadowRay_Casper"
};

// Hit group
const wchar_t* g_HitGroupNames[NUM_RAYTRACING_SHADER_TYPES] =
{
	L"MyHitGroup_Triangle_RadianceRay",
	L"MyHitGroup_Triangle_ShadowRay"
};
const wchar_t* g_HitGroupNames_Proc[NUM_RAYTRACING_SHADER_TYPES] =
{
	L"MyHitGroup_Proc_RadianceRay",
	L"MyHitGroup_Proc_ShadowRay"
};
const wchar_t* g_HitGroupNames_Casper[NUM_RAYTRACING_SHADER_TYPES] =
{
	L"MyHitGroup_Casper_RadianceRay",
	L"MyHitGroup_Casper_ShadowRay"
};

bool RayTracingManager::Initialize(D3D12Renderer* pRenderer, uint width, uint height, uint maxNumBLASs)
{
	HRESULT hr = S_OK;

	m_pRenderer = pRenderer;
	m_pD3DDevice = pRenderer->GetD3DDevice();
	ShaderManager* pShaderManager = pRenderer->GetShaderManager();

	m_MaxNumBLASs = maxNumBLASs;

	m_MaxNumShaderVisibleHeapDescriptors = DISPATCH_DESCRIPTOR_INDEX_COUNT + (LOCAL_ROOT_PARAM_DESCRIPTOR_COUNT * MAX_TRIGROUP_COUNT_PER_BLAS * m_MaxNumBLASs);

	m_pIndexCreator = new CIndexCreator;
	m_pIndexCreator->Initialize(m_MaxNumBLASs);

	m_pResourceBinTLAS = new D3D12ResourceRecycleBin;
	m_pResourceBinTLAS->Initialize(m_pD3DDevice, D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE, L"TopLevelAccelerationStructure");

	m_pResourceBinBLAS = new D3D12ResourceRecycleBin;
	m_pResourceBinBLAS->Initialize(m_pD3DDevice, D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE, L"BottomLevelAccelerationStructure");

	m_pResourceBinScratchResource = new D3D12ResourceRecycleBin;
	m_pResourceBinScratchResource->Initialize(m_pD3DDevice, D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COMMON, L"ScratchResource");

	m_pResourceBinTLASInstanceDescList = new D3D12ResourceRecycleBin;
	m_pResourceBinTLASInstanceDescList->Initialize(m_pD3DDevice, D3D12_HEAP_TYPE_UPLOAD, D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_GENERIC_READ, L"InstanceDesces");

	m_Width = width;
	m_Height = height;

	createDescriptorHeapCBV_SRV_UAV();
	createShaderVisibleHeap(m_MaxNumShaderVisibleHeapDescriptors);

	m_pRayShader = pShaderManager->CreateShaderDXC(L"Raytracing.hlsl", L"", L"lib_6_3", 0);
	m_pRayProcShader = pShaderManager->CreateShaderDXC(L"Raytracing_Proc.hlsl", L"", L"lib_6_3", 0);
	m_pRayCasperShader = pShaderManager->CreateShaderDXC(L"Raytracing_Casper.hlsl", L"", L"lib_6_3", 0);

	createOutputDiffuseBuffer(m_Width, m_Height);
	createOutputDepthBuffer(m_Width, m_Height);

	createRaytracingPipelineStateObject();

	buildShaderTables();

	for (uint i = 0; i < MAX_RENDER_THREAD_COUNT; ++i)
	{
		m_BLASInstanceListThisFrame[i].reserve(m_MaxNumBLASs);
	}

	return true;
}

void RayTracingManager::Cleanup()
{
	ShaderManager* pShaderManager = m_pRenderer->GetShaderManager();

	cleanupOutputDiffuseBuffer();
	cleanupOutputDepthBuffer();

	cleanupShaderTables();

	SAFE_RELEASE(m_pDXRStateObject);
	SAFE_CLEANUP(m_pRayShader, pShaderManager->ReleaseShader);
	SAFE_CLEANUP(m_pRayProcShader, pShaderManager->ReleaseShader);

	for (auto& blasHandle : m_GlobalBLASHandleList)
	{
		FreeBLAS(blasHandle);
	}

	if (m_pTLAS)
	{
		m_pResourceBinTLAS->Free(m_pTLAS, 1);
		m_pTLAS = nullptr;
	}
	if (m_pBLASInstanceDescResouce)
	{
		m_pResourceBinTLASInstanceDescList->Free(m_pBLASInstanceDescResouce, 1);
		m_pBLASInstanceDescResouce = nullptr;
	}

	cleanupDescriptorHeapCBV_SRV_UAV();
	cleanupDispatchHeap();

	SAFE_DELETE(m_pResourceBinTLAS);
	SAFE_DELETE(m_pResourceBinBLAS);
	SAFE_DELETE(m_pResourceBinScratchResource);
	SAFE_DELETE(m_pResourceBinTLASInstanceDescList);

	SAFE_DELETE(m_pIndexCreator);
}

void RayTracingManager::DoRaytracing(ID3D12GraphicsCommandList6* pCommandList)
{
	SimpleConstantBufferPool* pConstantBufferPool = m_pRenderer->GetConstantBufferPool(CONSTANT_BUFFER_TYPE_PER_FRAME, 0);
	SimpleConstantBufferPool* pConstantBufferPoolAtmos = m_pRenderer->GetConstantBufferPool(CONSTANT_BUFFER_TYPE_ATMOS, 0);
	ConstantBufferContainer* pCB = pConstantBufferPool->Alloc();
	ConstantBufferContainer* pCBAtmos = pConstantBufferPoolAtmos->Alloc();

	CD3DX12_CPU_DESCRIPTOR_HANDLE dispatchHeapHandleCPU(m_pShaderVisibleDescriptorHeap->GetCPUDescriptorHandleForHeapStart());
	CD3DX12_GPU_DESCRIPTOR_HANDLE dispatchHeapHandleGPU(m_pShaderVisibleDescriptorHeap->GetGPUDescriptorHandleForHeapStart());

	// Set up global constant buffers 
	{
		ASSERT(pCB, "Failed to allocate constant buffer.");
		ASSERT(pCBAtmos, "Failed to allocate constant buffer.");
		CONSTANT_BUFFER_PER_FRAME* pCBPerFrame = (CONSTANT_BUFFER_PER_FRAME*)pCB->pSystemMemAddr;
		CONSTANT_BUFFER_PER_FRAME srcCBData = m_pRenderer->GetFrameCBData();
		std::memcpy(pCBPerFrame, &srcCBData, sizeof(CONSTANT_BUFFER_PER_FRAME));
		pCBPerFrame->MaxRadianceRayRecursionDepth = GetMaxRadianceRecursionDepth();
		pCBPerFrame->MaxShadowRayRecursionDepth = GetMaxShadowRecursionDepth();

		CONSTANT_BUFFER_ATMOS* pCBAtmosData = (CONSTANT_BUFFER_ATMOS*)pCBAtmos->pSystemMemAddr;
		CONSTANT_BUFFER_ATMOS srcCBAtmosData = m_pRenderer->GetAtmosCBData();
		std::memcpy(pCBAtmosData, &srcCBAtmosData, sizeof(CONSTANT_BUFFER_ATMOS));
	}
	// Set up the dispatch descriptor heap
	{
		// (1) UAVs - Output Buffers
		CD3DX12_CPU_DESCRIPTOR_HANDLE uavDiffuse(m_pCommonDescriptorHeap->GetCPUDescriptorHandleForHeapStart(), COMMON_DESCRIPTOR_INDEX_OUTPUT_DIFFUSE_UAV, m_DescriptorSize);
		CD3DX12_CPU_DESCRIPTOR_HANDLE uavDepth(m_pCommonDescriptorHeap->GetCPUDescriptorHandleForHeapStart(), COMMON_DESCRIPTOR_INDEX_OUTPUT_DEPTH_UAV, m_DescriptorSize);

		CD3DX12_CPU_DESCRIPTOR_HANDLE uavHandle(dispatchHeapHandleCPU, 0, m_DescriptorSize);
		m_pD3DDevice->CopyDescriptorsSimple(1, uavHandle, uavDiffuse, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
		uavHandle.Offset(1, m_DescriptorSize);
		m_pD3DDevice->CopyDescriptorsSimple(1, uavHandle, uavDepth, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
		uavHandle.Offset(1, m_DescriptorSize);

		// (2) SRV - 
		CD3DX12_CPU_DESCRIPTOR_HANDLE srvHandle(dispatchHeapHandleCPU, 2, m_DescriptorSize);

		// (3) SRV - Sky Textures
		const TextureHandle* skyTransmittanceTexture = m_pRenderer->GetSkyTransmittanceTexture();
		const TextureHandle* skyIrradianceTexture = m_pRenderer->GetSkyIrradianceTexture();
		const TextureHandle* skyScatteringTexture = m_pRenderer->GetSkyScatteringTexture();

		CD3DX12_CPU_DESCRIPTOR_HANDLE srvSkyHandle(dispatchHeapHandleCPU, 2 + 10, m_DescriptorSize);
		m_pD3DDevice->CopyDescriptorsSimple(1, srvSkyHandle, skyTransmittanceTexture->SRV, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
		srvSkyHandle.Offset(1, m_DescriptorSize);
		m_pD3DDevice->CopyDescriptorsSimple(1, srvSkyHandle, skyScatteringTexture->SRV, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
		srvSkyHandle.Offset(1, m_DescriptorSize);
		m_pD3DDevice->CopyDescriptorsSimple(1, srvSkyHandle, skyIrradianceTexture->SRV, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
		srvSkyHandle.Offset(1, m_DescriptorSize);
	}

	pCommandList->SetComputeRootSignature(m_pRenderer->GetRootSignatureManager()->Query(ERootSignatureType::GraphicsRaytracingGlobal));

	// Bind the heaps, acceleration structure and dispatch rays.    
	D3D12_DISPATCH_RAYS_DESC dispatchDesc = {};
	ID3D12DescriptorHeap* ppHeaps[] = { m_pShaderVisibleDescriptorHeap };
	pCommandList->SetDescriptorHeaps(_countof(ppHeaps), ppHeaps);

	pCommandList->SetComputeRootConstantBufferView(0, pCB->pGPUMemAddr);
	pCommandList->SetComputeRootConstantBufferView(1, pCBAtmos->pGPUMemAddr);
	pCommandList->SetComputeRootDescriptorTable(2, dispatchHeapHandleGPU);
	pCommandList->SetComputeRootShaderResourceView(3, m_pTLAS->GetGPUVirtualAddress());

	// hit group shader table
	ID3D12Resource* pHitGroupShaderTableResource = m_pHitGroupShaderTable->GetResource();
	dispatchDesc.HitGroupTable.StartAddress = pHitGroupShaderTableResource->GetGPUVirtualAddress();
	dispatchDesc.HitGroupTable.SizeInBytes = m_pHitGroupShaderTable->GetHitGroupShaderTableSize();
	dispatchDesc.HitGroupTable.StrideInBytes = m_HitGroupShaderTableStrideInBytes;

	// miss shader table
	ID3D12Resource* pMissShaderTableResource = m_pMissShaderTable->GetResource();
	dispatchDesc.MissShaderTable.StartAddress = pMissShaderTableResource->GetGPUVirtualAddress();
	dispatchDesc.MissShaderTable.SizeInBytes = m_pMissShaderTable->GetHitGroupShaderTableSize();
	dispatchDesc.MissShaderTable.StrideInBytes = m_MissShaderTableStrideInBytes;

	// raygen shader table
	ID3D12Resource* pRayGenShaderTableResource = m_pRayGenShaderTable->GetResource();
	dispatchDesc.RayGenerationShaderRecord.StartAddress = pRayGenShaderTableResource->GetGPUVirtualAddress();
	dispatchDesc.RayGenerationShaderRecord.SizeInBytes = m_pRayGenShaderTable->GetShaderRecordSize();

	dispatchDesc.Width = m_Width;
	dispatchDesc.Height = m_Height;
	dispatchDesc.Depth = 1;

	pCommandList->SetPipelineState1(m_pDXRStateObject);
	pCommandList->DispatchRays(&dispatchDesc);

	// Clear BLAS instance lists
	for (uint i = 0; i < MAX_RENDER_THREAD_COUNT; ++i)
	{
		m_BLASInstanceListThisFrame[i].clear();
	}
}

BLASHandle* RayTracingManager::AllocBLASTriangles(
	ID3D12Resource* pVertexBuffer,
	uint numVertices,
	uint vertexStrideBytes,
	const std::vector<MeshSection>& triGroups,
	const std::vector<bool>& bTriGroupOpaques,
	bool bAllowUpdate)
{
	BLASHandle* pBLASHandle = nullptr;
	ID3D12Device5* pD3DDevice = m_pRenderer->GetD3DDevice();

	if (m_GlobalBLASHandleList.size() >= m_MaxNumBLASs)
	{
		ASSERT(false, "Exceeded maximum number of BLAS instances.");
		goto lb_return;
	}

	const uint numTriGroupInfos = static_cast<uint>(triGroups.size());
	ASSERT(numTriGroupInfos < MAX_TRIGROUP_COUNT_PER_BLAS, "Too many triangle groups in BLAS");

	uint32_t index = m_pIndexCreator->Alloc();
	ASSERT(index != -1, "Failed to allocate index for BLAS instance.");

	// Create BLAS Handle
	pBLASHandle = new BLASHandle;
	pBLASHandle->pBLAS = nullptr; // BLAS는 아직 생성되지 않음.
	pBLASHandle->ID = index;
	pBLASHandle->bAllowUpdate = bAllowUpdate;
	pBLASHandle->ShaderRecordIndex = std::numeric_limits<uint32_t>::max(); // 아직 할당되지 않음.
	pBLASHandle->Kind = BLASHandle::GeomKind::Triangles;
	pBLASHandle->NumVertices = numVertices;
	pBLASHandle->NumTriGroups = numTriGroupInfos;

	// Fill Geometry Descriptions
	D3D12_RAYTRACING_GEOMETRY_DESC* pGeomDescList = pBLASHandle->pGeomDescList;
	D3D12_GPU_VIRTUAL_ADDRESS VB_GPU_Ptr = pVertexBuffer->GetGPUVirtualAddress();
	for (uint i = 0; i < numTriGroupInfos; ++i)
	{
		D3D12_GPU_VIRTUAL_ADDRESS IB_GPU_Ptr = triGroups[i].IndexBuffer->GetGPUVirtualAddress();

		pGeomDescList[i].Type = D3D12_RAYTRACING_GEOMETRY_TYPE_TRIANGLES;
		pGeomDescList[i].Triangles.IndexBuffer = IB_GPU_Ptr;
		pGeomDescList[i].Triangles.IndexCount = triGroups[i].NumTriangles * 3;
		pGeomDescList[i].Triangles.IndexFormat = DXGI_FORMAT_R16_UINT;
		pGeomDescList[i].Triangles.Transform3x4 = 0;
		pGeomDescList[i].Triangles.VertexFormat = DXGI_FORMAT_R32G32B32_FLOAT;
		pGeomDescList[i].Triangles.VertexCount = numVertices;
		pGeomDescList[i].Triangles.VertexBuffer.StartAddress = VB_GPU_Ptr;
		pGeomDescList[i].Triangles.VertexBuffer.StrideInBytes = vertexStrideBytes;
		// Mark the geometry as opaque. 
		// PERFORMANCE TIP: mark geometry as opaque whenever applicable as it can enable important ray processing optimizations.
		// Note: When rays encounter opaque geometry an any hit shader will not be executed whether it is present or not.
		// pGeomDescList[i].Flags = pTriGroupInfoList[i].bOpaque ? D3D12_RAYTRACING_GEOMETRY_FLAG_OPAQUE : D3D12_RAYTRACING_GEOMETRY_FLAG_NONE;
		pGeomDescList[i].Flags = bTriGroupOpaques[i] ? D3D12_RAYTRACING_GEOMETRY_FLAG_OPAQUE : D3D12_RAYTRACING_GEOMETRY_FLAG_NONE;
	}

	// Set Local Root Parameters
	{
		pBLASHandle->RootArgArray.resize(numTriGroupInfos);
		UINT descriptorIndex = DISPATCH_DESCRIPTOR_INDEX_COUNT + (LOCAL_ROOT_PARAM_DESCRIPTOR_COUNT * pBLASHandle->ID * MAX_TRIGROUP_COUNT_PER_BLAS);

		for (uint i = 0; i < numTriGroupInfos; ++i)
		{
			// ---------- space1: t0..t3 (VB, IB, Diffuse, Normal)
			CD3DX12_CPU_DESCRIPTOR_HANDLE cpuS1(m_pShaderVisibleDescriptorHeap->GetCPUDescriptorHandleForHeapStart(), descriptorIndex, m_DescriptorSize);
			CD3DX12_GPU_DESCRIPTOR_HANDLE gpuS1(m_pShaderVisibleDescriptorHeap->GetGPUDescriptorHandleForHeapStart(), descriptorIndex, m_DescriptorSize);

			D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
			srvDesc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
			srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;

			// t0: VB (structured)
			srvDesc.Buffer.FirstElement = 0;
			srvDesc.Buffer.NumElements = numVertices;
			srvDesc.Format = DXGI_FORMAT_UNKNOWN;
			srvDesc.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_NONE;
			srvDesc.Buffer.StructureByteStride = vertexStrideBytes;
			pD3DDevice->CreateShaderResourceView(pVertexBuffer, &srvDesc, cpuS1);
			cpuS1.Offset(1, m_DescriptorSize);

			// t1: IB (RAW, R32_TYPELESS) — ★NumElements 올림!
			srvDesc = {};
			srvDesc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
			srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
			srvDesc.Format = DXGI_FORMAT_R32_TYPELESS;
			srvDesc.Buffer.FirstElement = 0;
			{
				const uint indexCount = triGroups[i].NumTriangles * 3;  // 16-bit index 개수
				const uint bytesTotal = indexCount * 2;                  // 총 바이트
				srvDesc.Buffer.NumElements = (bytesTotal + 3) / 4;       // ← 4바이트 단위 '올림'
			}
			srvDesc.Buffer.StructureByteStride = 0;
			srvDesc.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_RAW;
			pD3DDevice->CreateShaderResourceView(triGroups[i].IndexBuffer, &srvDesc, cpuS1);
			cpuS1.Offset(1, m_DescriptorSize);

			// t2: Diffuse
			if (triGroups[i].DiffuseTexHandle && triGroups[i].DiffuseTexHandle->SRV.ptr)
			{
				pD3DDevice->CopyDescriptorsSimple(1, cpuS1, triGroups[i].DiffuseTexHandle->SRV, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
			}
			cpuS1.Offset(1, m_DescriptorSize);

			// t3: Normal
			if (triGroups[i].NormalTexHandle && triGroups[i].NormalTexHandle->SRV.ptr)\
			{
				pD3DDevice->CopyDescriptorsSimple(1, cpuS1, triGroups[i].NormalTexHandle->SRV, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
			}
			cpuS1.Offset(1, m_DescriptorSize);

			pBLASHandle->RootArgArray[i].SrvTable = gpuS1;

			// Constants
			pBLASHandle->RootArgArray[i].Constants.Material = triGroups[i].Material;

			descriptorIndex += 4;
		}
	}

	m_GlobalBLASHandleList.emplace_back(pBLASHandle);
	m_UpdateAccelerationStructureFlags = UPDATE_ACCELERATION_STRCTURE_TYPE_HIT_GROUP_SHADER_TABLE | UPDATE_ACCELERATION_STRCTURE_TYPE_TLAS;
lb_return:
	return pBLASHandle;
}

BLASHandle* RayTracingManager::AllocBLASSpheres(
	ID3D12Resource* pAABBBuffer,
	uint numAABBs,
	uint aabbStrideBytes,
	ID3D12Resource* pSphereDataBuffer,
	uint numSpheres,
	uint sphereDataStrideBytes,
	const CBMaterial& material,
	bool bOpaque,
	bool bAllowUpdate)
{
	BLASHandle* pBLASHandle = nullptr;
	ID3D12Device5* pD3DDevice = m_pRenderer->GetD3DDevice();

	if (m_GlobalBLASHandleList.size() >= m_MaxNumBLASs)
	{
		ASSERT(false, "Exceeded maximum number of BLAS instances.");
		return nullptr;
	}
	ASSERT(numAABBs < MAX_TRIGROUP_COUNT_PER_BLAS, "Too many AABBs in BLAS");

	uint32_t index = m_pIndexCreator->Alloc();
	ASSERT(index != (uint32_t)-1, "Failed to allocate index for BLAS instance.");

	pBLASHandle = new BLASHandle{};
	pBLASHandle->pBLAS = nullptr;
	pBLASHandle->ID = index;
	pBLASHandle->bAllowUpdate = bAllowUpdate;
	pBLASHandle->ShaderRecordIndex = std::numeric_limits<uint32_t>::max();
	pBLASHandle->Kind = BLASHandle::GeomKind::Procedural;
	pBLASHandle->NumVertices = 0;
	pBLASHandle->NumTriGroups = 1;

	// Fill AABB geometry descs
	D3D12_RAYTRACING_GEOMETRY_DESC* pGeomDesc = pBLASHandle->pGeomDescList;
	pGeomDesc->Type = D3D12_RAYTRACING_GEOMETRY_TYPE_PROCEDURAL_PRIMITIVE_AABBS;
	pGeomDesc->AABBs.AABBCount = numAABBs;
	pGeomDesc->AABBs.AABBs.StartAddress = pAABBBuffer->GetGPUVirtualAddress();
	pGeomDesc->AABBs.AABBs.StrideInBytes = aabbStrideBytes; // 보통 sizeof(D3D12_RAYTRACING_AABB)=24
	pGeomDesc->Flags = bOpaque ? D3D12_RAYTRACING_GEOMETRY_FLAG_OPAQUE : D3D12_RAYTRACING_GEOMETRY_FLAG_NONE;

	// Local root: SRV 테이블(space1) 구성
	{
		// pBLASHandle->RootArgArray.resize(numAABBs);
		pBLASHandle->RootArgArray.resize(1);

		UINT descriptorIndex = DISPATCH_DESCRIPTOR_INDEX_COUNT + (LOCAL_ROOT_PARAM_DESCRIPTOR_COUNT * pBLASHandle->ID * MAX_TRIGROUP_COUNT_PER_BLAS);

		// ---- space1 (t0..t1): Sphere data buffer, AABB buffer ----
		CD3DX12_CPU_DESCRIPTOR_HANDLE cpuS1(m_pShaderVisibleDescriptorHeap->GetCPUDescriptorHandleForHeapStart(), descriptorIndex, m_DescriptorSize);
		CD3DX12_GPU_DESCRIPTOR_HANDLE gpuS1(m_pShaderVisibleDescriptorHeap->GetGPUDescriptorHandleForHeapStart(), descriptorIndex, m_DescriptorSize);

		D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};

		// t0: SphereParamBuffer (structured)
		srvDesc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
		srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
		srvDesc.Format = DXGI_FORMAT_UNKNOWN;
		srvDesc.Buffer.FirstElement = 0;
		srvDesc.Buffer.NumElements = numSpheres;
		srvDesc.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_NONE;
		srvDesc.Buffer.StructureByteStride = sphereDataStrideBytes;
		m_pD3DDevice->CreateShaderResourceView(pSphereDataBuffer, &srvDesc, cpuS1);
		cpuS1.Offset(1, m_DescriptorSize);

		// t1: AABB buffer (structured)
		srvDesc = {};
		srvDesc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
		srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
		srvDesc.Format = DXGI_FORMAT_UNKNOWN;
		srvDesc.Buffer.FirstElement = 0;
		srvDesc.Buffer.NumElements = numAABBs;
		srvDesc.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_NONE;
		srvDesc.Buffer.StructureByteStride = aabbStrideBytes;
		m_pD3DDevice->CreateShaderResourceView(pAABBBuffer, &srvDesc, cpuS1);

		pBLASHandle->RootArgArray[0].SrvTable = gpuS1;

		// 상수(필요시): Proc에 맞는 머티리얼/옵션 세팅
		pBLASHandle->RootArgArray[0].Constants.Material = material;
	}

	m_GlobalBLASHandleList.emplace_back(pBLASHandle);
	m_UpdateAccelerationStructureFlags = UPDATE_ACCELERATION_STRCTURE_TYPE_HIT_GROUP_SHADER_TABLE | UPDATE_ACCELERATION_STRCTURE_TYPE_TLAS;
	return pBLASHandle;
}

BLASHandle* RayTracingManager::AllocBLASCasper(
	ID3D12Resource* pAABBBuffer,
	uint numAABBs,
	uint aabbStrideBytes,
	const std::vector<std::pair<TextureHandle*, TextureHandle*>>& atlases,
	const CBMaterial& material,
	bool bOpaque,
	bool bAllowUpdate)
{
	BLASHandle* pBLASHandle = nullptr;
	ID3D12Device5* pD3DDevice = m_pRenderer->GetD3DDevice();

	if (m_GlobalBLASHandleList.size() >= m_MaxNumBLASs)
	{
		ASSERT(false, "Exceeded maximum number of BLAS instances.");
		return nullptr;
	}
	ASSERT(numAABBs < MAX_TRIGROUP_COUNT_PER_BLAS, "Too many AABBs in BLAS");

	uint32_t index = m_pIndexCreator->Alloc();
	ASSERT(index != (uint32_t)-1, "Failed to allocate index for BLAS instance.");

	pBLASHandle = new BLASHandle{};
	pBLASHandle->pBLAS = nullptr;
	pBLASHandle->ID = index;
	pBLASHandle->bAllowUpdate = bAllowUpdate;
	pBLASHandle->ShaderRecordIndex = std::numeric_limits<uint32_t>::max();
	pBLASHandle->Kind = BLASHandle::GeomKind::Casper;
	pBLASHandle->NumVertices = 0;
	pBLASHandle->NumTriGroups = 1;

	// Fill AABB geometry descs
	D3D12_RAYTRACING_GEOMETRY_DESC* pGeomDesc = pBLASHandle->pGeomDescList;
	pGeomDesc->Type = D3D12_RAYTRACING_GEOMETRY_TYPE_PROCEDURAL_PRIMITIVE_AABBS;
	pGeomDesc->AABBs.AABBCount = numAABBs;
	pGeomDesc->AABBs.AABBs.StartAddress = pAABBBuffer->GetGPUVirtualAddress();
	pGeomDesc->AABBs.AABBs.StrideInBytes = aabbStrideBytes; // 보통 sizeof(D3D12_RAYTRACING_AABB)=24
	pGeomDesc->Flags = bOpaque ? D3D12_RAYTRACING_GEOMETRY_FLAG_OPAQUE : D3D12_RAYTRACING_GEOMETRY_FLAG_NONE;

	// Local root: SRV 테이블(space1) 구성
	{
		// pBLASHandle->RootArgArray.resize(numAABBs);
		pBLASHandle->RootArgArray.resize(1);

		UINT descriptorIndex = DISPATCH_DESCRIPTOR_INDEX_COUNT + (LOCAL_ROOT_PARAM_DESCRIPTOR_COUNT * pBLASHandle->ID * MAX_TRIGROUP_COUNT_PER_BLAS);

		// ---- space1 (t0..t2): CASPER atlas textures, AABB buffer ----
		for (size_t i = 0; i < atlases.size(); ++i)
		{
			CD3DX12_CPU_DESCRIPTOR_HANDLE cpuS1(m_pShaderVisibleDescriptorHeap->GetCPUDescriptorHandleForHeapStart(), descriptorIndex, m_DescriptorSize);
			CD3DX12_GPU_DESCRIPTOR_HANDLE gpuS1(m_pShaderVisibleDescriptorHeap->GetGPUDescriptorHandleForHeapStart(), descriptorIndex, m_DescriptorSize);

			auto [pDiffuse, pDepth] = atlases[i];
			// t0: Diffuse
			pD3DDevice->CopyDescriptorsSimple(1, cpuS1, pDiffuse->SRV, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
			cpuS1.Offset(1, m_DescriptorSize);
			// t1: Depth
			pD3DDevice->CopyDescriptorsSimple(1, cpuS1, pDepth->SRV, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
			cpuS1.Offset(1, m_DescriptorSize);

			// t2: AABB buffer (structured)
			D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc{};
			srvDesc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
			srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
			srvDesc.Format = DXGI_FORMAT_UNKNOWN;
			srvDesc.Buffer.FirstElement = 0;
			srvDesc.Buffer.NumElements = numAABBs;
			srvDesc.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_NONE;
			srvDesc.Buffer.StructureByteStride = aabbStrideBytes;
			m_pD3DDevice->CreateShaderResourceView(pAABBBuffer, &srvDesc, cpuS1);
			cpuS1.Offset(1, m_DescriptorSize);

			pBLASHandle->RootArgArray[i].SrvTable = gpuS1;

			pBLASHandle->RootArgArray[i].Constants.Material = material;

			descriptorIndex += 4;
		}
	}

	m_GlobalBLASHandleList.emplace_back(pBLASHandle);
	m_UpdateAccelerationStructureFlags = UPDATE_ACCELERATION_STRCTURE_TYPE_HIT_GROUP_SHADER_TABLE | UPDATE_ACCELERATION_STRCTURE_TYPE_TLAS;
	return pBLASHandle;
}

void RayTracingManager::FreeBLAS(BLASHandle* pBLASHandle)
{
	ASSERT(pBLASHandle, "Invalid BLAS handle to free.");

	m_pIndexCreator->Free(pBLASHandle->ID);
	pBLASHandle->ID = -1;

	if (pBLASHandle->pBLAS)
	{
		m_pResourceBinBLAS->Free(pBLASHandle->pBLAS, MAX_PENDING_FRAME_COUNT);
		pBLASHandle->pBLAS = nullptr;
	}

	auto iter = std::find(m_GlobalBLASHandleList.begin(), m_GlobalBLASHandleList.end(), pBLASHandle);
	m_GlobalBLASHandleList.erase(iter);

	SAFE_FREE(pBLASHandle);
	m_UpdateAccelerationStructureFlags = UPDATE_ACCELERATION_STRCTURE_TYPE_HIT_GROUP_SHADER_TABLE | UPDATE_ACCELERATION_STRCTURE_TYPE_TLAS;
}

bool RayTracingManager::UpdateAccelerationStructure(ID3D12GraphicsCommandList6* pCommandList)
{
	// (1) HitGroup ShaderTable은 "유일한 BLAS 집합" 기준으로만 필요할 때 갱신
	uint numRequiredShaderRecordCount = 0;
	for (BLASHandle* curr : m_GlobalBLASHandleList)
	{
		ASSERT(curr->pBLAS, "BLAS must be built before updating TLAS");
		numRequiredShaderRecordCount += curr->NumTriGroups * NUM_RAYTRACING_SHADER_TYPES;
	}

	if (m_UpdateAccelerationStructureFlags & UPDATE_ACCELERATION_STRCTURE_TYPE_HIT_GROUP_SHADER_TABLE)
	{
		// HitGroupShaderTable갱신과 함께 BLAS별로 ShderReocordIndex를 설정한다.
		updateHitGroupShaderTable(numRequiredShaderRecordCount);
		m_UpdateAccelerationStructureFlags &= (~UPDATE_ACCELERATION_STRCTURE_TYPE_HIT_GROUP_SHADER_TABLE);
	}

	// (2) TLAS는 인스턴스 리스트로 빌드
	if (m_UpdateAccelerationStructureFlags & UPDATE_ACCELERATION_STRCTURE_TYPE_TLAS)
	{
		if (m_pTLAS)
		{
			m_pResourceBinTLAS->Free(m_pTLAS, MAX_PENDING_FRAME_COUNT);
			m_pTLAS = nullptr;
		}

		if (m_pBLASInstanceDescResouce)
		{
			m_pResourceBinTLASInstanceDescList->Free(m_pBLASInstanceDescResouce, MAX_PENDING_FRAME_COUNT);
			m_pBLASInstanceDescResouce = nullptr;
		}

		m_pTLAS = buildTLAS(pCommandList, false, 0);
		m_UpdateAccelerationStructureFlags &= (~UPDATE_ACCELERATION_STRCTURE_TYPE_TLAS);
	}

	return true;
}

void RayTracingManager::UpdateManagedResource()
{
	uint64_t currTick = static_cast<uint64_t>(GetTickCount64());
	m_pResourceBinTLAS->Update(currTick);
	m_pResourceBinBLAS->Update(currTick);
	m_pResourceBinScratchResource->Update(currTick);
	m_pResourceBinTLASInstanceDescList->Update(currTick);
}

void RayTracingManager::UpdateBLAS(int threadIndex, ID3D12GraphicsCommandList6* pCommandList, BLASHandle* pBLASHandle, const Matrix4x4& worldMatrix)
{
	if (!pBLASHandle->pBLAS)
	{
		bool bBuilt = buildBLAS(pCommandList, pBLASHandle);
		ASSERT(bBuilt, "Failed to build BLAS.");
	}
	ASSERT(pBLASHandle->pBLAS, "BLAS must be built before updating transform.");

	BLASInstance blasInstance = {};
	blasInstance.pBLASHandle = pBLASHandle;
	blasInstance.Transform = worldMatrix;
	blasInstance.InstanceMask = 0xFF;
	blasInstance.Flags = D3D12_RAYTRACING_INSTANCE_FLAG_NONE;

	m_BLASInstanceListThisFrame[threadIndex].emplace_back(blasInstance);

	m_UpdateAccelerationStructureFlags |= UPDATE_ACCELERATION_STRCTURE_TYPE_TLAS;
}

void RayTracingManager::UpdateWindowSize(uint width, uint height)
{
	cleanupOutputDiffuseBuffer();
	cleanupOutputDepthBuffer();

	m_Width = width;
	m_Height = height;
	createOutputDiffuseBuffer(m_Width, m_Height);
	createOutputDepthBuffer(m_Width, m_Height);
}

bool RayTracingManager::buildBLAS(ID3D12GraphicsCommandList6* pCommandList, BLASHandle* pBLASHandle)
{
	bool bResult = false;
	ID3D12Device5* pD3DDevice = m_pRenderer->GetD3DDevice();

	ASSERT(pBLASHandle->NumTriGroups < MAX_TRIGROUP_COUNT_PER_BLAS, "Too many triangle groups in BLAS");

	if (pBLASHandle->pBLAS)
	{
		m_pResourceBinBLAS->Free(pBLASHandle->pBLAS, MAX_PENDING_FRAME_COUNT);
		pBLASHandle->pBLAS = nullptr;
	}

	// Build BLAS
	// Get required sizes for an acceleration structure.
	D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS inputs = {};
	inputs.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY;
	if (pBLASHandle->bAllowUpdate)
	{
		inputs.Flags = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_BUILD | D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_ALLOW_UPDATE;
	}
	else
	{
		inputs.Flags = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_TRACE | D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_ALLOW_COMPACTION;
	}
	inputs.NumDescs = pBLASHandle->NumTriGroups;
	inputs.pGeometryDescs = pBLASHandle->pGeomDescList;
	inputs.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL;

	D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO info = {};
	pD3DDevice->GetRaytracingAccelerationStructurePrebuildInfo(&inputs, &info);

	ID3D12Resource* pScratchResource = m_pResourceBinScratchResource->Alloc(info.ScratchDataSizeInBytes);
	ASSERT(pScratchResource, "Failed to allocate scratch resource for BLAS.");

	D3D12_GPU_VIRTUAL_ADDRESS pScratchGPUAddress = pScratchResource->GetGPUVirtualAddress();
	ASSERT(pScratchGPUAddress, "Invalid GPU address for scratch resource.");

	// Allocate resources for acceleration structures.
	// Acceleration structures can only be placed in resources that are created in the default heap (or custom heap equivalent). 
	// Default heap is OK since the application doesn't need CPU read/write access to them. 
	// The resources that will contain acceleration structures must be created in the state D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE, 
	// and must have resource flag D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS. The ALLOW_UNORDERED_ACCESS requirement simply acknowledges both: 
	//  - the system will be doing this type of access in its implementation of acceleration structure builds behind the scenes.
	//  - from the app point of view, synchronization of writes/reads to acceleration structures is accomplished using UAV barriers.
	// 
	//D3D12_RESOURCE_STATES initialResourceState = D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE;
	//if (FAILED(CreateUAVBuffer(m_pD3DDevice, info.ResultDataMaxSizeInBytes, &pBLAS, initialResourceState, L"BottomLevelAccelerationStructure")))
	//	__debugbreak();
	pBLASHandle->pBLAS = m_pResourceBinBLAS->Alloc(info.ResultDataMaxSizeInBytes);

	// Bottom Level Acceleration Structure desc
	D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC asDesc = {};
	asDesc.Inputs = inputs;
	asDesc.ScratchAccelerationStructureData = pScratchGPUAddress;
	asDesc.DestAccelerationStructureData = pBLASHandle->pBLAS->GetGPUVirtualAddress();

	pCommandList->BuildRaytracingAccelerationStructure(&asDesc, 0, nullptr);
	// We need to insert a UAV barrier before using the acceleration structures in a raytracing operation
	pCommandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::UAV(pBLASHandle->pBLAS));

	if (pScratchResource)
	{
		m_pResourceBinScratchResource->Free(pScratchResource, MAX_PENDING_FRAME_COUNT);
		pScratchResource = nullptr;
	}

	bResult = true;
	return bResult;
}

ID3D12Resource* RayTracingManager::buildTLAS(ID3D12GraphicsCommandList6* pCommandList, bool bAllowUpdate, uint currContextIndex)
{
	ID3D12Resource* pTLASResource = nullptr;

	// Collect all BLAS instances from all threads
	uint numBLASInstances = 0;
	for (uint i = 0; i < MAX_RENDER_THREAD_COUNT; ++i)
	{
		numBLASInstances += static_cast<uint>(m_BLASInstanceListThisFrame[i].size());
	}
	m_pBLASInstanceDescResouce = m_pResourceBinTLASInstanceDescList->Alloc(sizeof(D3D12_RAYTRACING_INSTANCE_DESC) * numBLASInstances);

	// Get the size of the TLAS buffers and create them
	D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS inputs = {};
	inputs.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY;
	inputs.Flags = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_NONE;
	if (bAllowUpdate)
	{
		//inputs.Flags |= D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_ALLOW_UPDATE;
		inputs.Flags |= D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_BUILD | D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_ALLOW_UPDATE;
	}
	else
	{
		inputs.Flags = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_TRACE | D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_ALLOW_COMPACTION;
	}
	inputs.NumDescs = numBLASInstances;
	inputs.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL;

	D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO info;
	m_pD3DDevice->GetRaytracingAccelerationStructurePrebuildInfo(&inputs, &info);

	ID3D12Resource* pScratchResource = m_pResourceBinScratchResource->Alloc(info.ScratchDataSizeInBytes);

	D3D12_GPU_VIRTUAL_ADDRESS pScratchGPUAddress = pScratchResource->GetGPUVirtualAddress();

	pTLASResource = m_pResourceBinTLAS->Alloc(info.ResultDataMaxSizeInBytes);
	ASSERT(pTLASResource, "Failed to allocate TLAS resource.");

	// Allocate resources for acceleration structures.
	// Acceleration structures can only be placed in resources that are created in the default heap (or custom heap equivalent). 
	// Default heap is OK since the application doesn't need CPU read/write access to them. 
	// The resources that will contain acceleration structures must be created in the state D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE, 
	// and must have resource flag D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS. The ALLOW_UNORDERED_ACCESS requirement simply acknowledges both: 
	//  - the system will be doing this type of access in its implementation of acceleration structure builds behind the scenes.
	//  - from the app point of view, synchronization of writes/reads to acceleration structures is accomplished using UAV barriers.
	D3D12_RAYTRACING_INSTANCE_DESC* pInstanceDescList = nullptr;

	CD3DX12_RANGE readRange(0, 0);
	m_pBLASInstanceDescResouce->Map(0, &readRange, (void**)&pInstanceDescList);

	uint numTlasElements = 0;
	D3D12_RAYTRACING_INSTANCE_DESC* pInstanceDescEntry = pInstanceDescList;
	for (uint i = 0; i < MAX_RENDER_THREAD_COUNT; ++i)
	{
		const std::vector<BLASInstance>& blasInstanceList = m_BLASInstanceListThisFrame[i];
		for (const BLASInstance& currInstance : blasInstanceList)
		{
			Matrix4x4 matTranspose = XMMatrixTranspose(currInstance.Transform);
			memcpy(pInstanceDescEntry->Transform, &matTranspose, sizeof(pInstanceDescEntry->Transform));
			pInstanceDescEntry->InstanceID = numTlasElements; // This value will be exposed to the shader via InstanceID()
			pInstanceDescEntry->InstanceContributionToHitGroupIndex = currInstance.pBLASHandle->ShaderRecordIndex;
			pInstanceDescEntry->Flags = currInstance.Flags;
			pInstanceDescEntry->AccelerationStructure = currInstance.pBLASHandle->pBLAS->GetGPUVirtualAddress();
			pInstanceDescEntry->InstanceMask = currInstance.InstanceMask;
			pInstanceDescEntry++;
			numTlasElements++;
		}
	}

	// Unmap
	m_pBLASInstanceDescResouce->Unmap(0, nullptr);

	// Create the TLAS
	D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC asDesc = {};
	asDesc.Inputs = inputs;
	asDesc.Inputs.NumDescs = numTlasElements;
	asDesc.Inputs.InstanceDescs = m_pBLASInstanceDescResouce->GetGPUVirtualAddress();
	asDesc.DestAccelerationStructureData = pTLASResource->GetGPUVirtualAddress();
	asDesc.ScratchAccelerationStructureData = pScratchGPUAddress;

	pCommandList->BuildRaytracingAccelerationStructure(&asDesc, 0, nullptr);

	// We need to insert a UAV barrier before using the acceleration structures in a raytracing operation
	D3D12_RESOURCE_BARRIER uavBarrier = {};
	uavBarrier.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
	uavBarrier.UAV.pResource = pTLASResource;
	pCommandList->ResourceBarrier(1, &uavBarrier);

	if (pScratchResource)
	{
		m_pResourceBinScratchResource->Free(pScratchResource, MAX_PENDING_FRAME_COUNT);
		pScratchResource = nullptr;
	}

	return pTLASResource;
}

void RayTracingManager::updateHitGroupShaderTable(uint numShaderRecords)
{
	//
	// wait필요
	//

	// ShaderRecord in HitGroup Table
	// |                 0               |                 1               | .... |                 N-1             |        
	// | [ShaderIdntifier-RootArguments] | [ShaderIdntifier-RootArguments] | .... | [ShaderIdntifier-RootArguments] |

	// Get shader identifiers.
	ID3D12StateObjectProperties* pStateObjectProperties = nullptr;
	m_pDXRStateObject->QueryInterface(IID_PPV_ARGS(&pStateObjectProperties));

	// hitgroup Shader Table
	// Tri & Proc hitgroup identifiers
	void* pHitGroupId_Tri[NUM_RAYTRACING_SHADER_TYPES] = {};
	void* pHitGroupId_Proc[NUM_RAYTRACING_SHADER_TYPES] = {};
	void* pHitGroupId_Casper[NUM_RAYTRACING_SHADER_TYPES] = {};
	for (uint i = 0; i < NUM_RAYTRACING_SHADER_TYPES; ++i)
	{
		pHitGroupId_Tri[i] = pStateObjectProperties->GetShaderIdentifier(g_HitGroupNames[i]);
		pHitGroupId_Proc[i] = pStateObjectProperties->GetShaderIdentifier(g_HitGroupNames_Proc[i]);
		pHitGroupId_Casper[i] = pStateObjectProperties->GetShaderIdentifier(g_HitGroupNames_Casper[i]);
		ASSERT(pHitGroupId_Tri[i], "Failed to get hit group shader identifier.");
		ASSERT(pHitGroupId_Proc[i], "Failed to get hit group shader identifier.");
		ASSERT(pHitGroupId_Casper[i], "Failed to get hit group shader identifier.");
	}
	m_pHitGroupShaderTable->CommitResource(numShaderRecords);

	uint shaderRecordIndex = 0;
	for (BLASHandle* curr : m_GlobalBLASHandleList)
	{
		// pBLASHandle->ShaderRecordIndex는 HitGroupShaderTable에서의 ShaderRecord 시작 인덱스.
		// 이 값은 TLAS빌드 시에 D3D12_RAYTRACING_INSTANCE_DESC::InstanceContributionToHitGroupIndex에 대입한다.
		curr->ShaderRecordIndex = shaderRecordIndex;
		for (uint i = 0; i < curr->NumTriGroups; ++i)
		{
			for (uint j = 0; j < NUM_RAYTRACING_SHADER_TYPES; ++j)
			{
				void* id = nullptr;
				void* rootArg = nullptr;
				size_t rootArgSize = 0;
				switch (curr->Kind)
				{
				case BLASHandle::GeomKind::Triangles:
					id = pHitGroupId_Tri[j];
					rootArg = (void*)(&curr->RootArgArray[i]);
					rootArgSize = sizeof(RootArgument);
					break;
				case BLASHandle::GeomKind::Procedural:
					id = pHitGroupId_Proc[j];
					rootArg = (void*)(&curr->RootArgArray[i]);
					rootArgSize = sizeof(RootArgument);
					break;
				case BLASHandle::GeomKind::Casper:
					id = pHitGroupId_Casper[j];
					rootArg = (void*)(&curr->RootArgArray[i]);
					rootArgSize = sizeof(RootArgument);
					break;
				default:
					ASSERT(false, "Unknown BLAS geometry kind.");
					break;
				}
				ShaderRecord record = ShaderRecord(id, m_ShaderIdentifierSize, rootArg, rootArgSize);
				m_pHitGroupShaderTable->InsertShaderRecord(&record);
				++shaderRecordIndex;
			}
		}
	}

	m_HitGroupShaderTableStrideInBytes = m_pHitGroupShaderTable->GetShaderRecordSize();
	m_HitGroupShaderRecordNum = m_pHitGroupShaderTable->GetShaderRecordNum();

	SAFE_RELEASE(pStateObjectProperties);
}

bool RayTracingManager::createOutputDiffuseBuffer(uint width, uint height)
{
	D3D12_RESOURCE_DESC texDesc = {};
	texDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
	texDesc.Alignment = 0;
	texDesc.Width = width;
	texDesc.Height = height;
	texDesc.DepthOrArraySize = 1;
	texDesc.MipLevels = 1;
	texDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	texDesc.SampleDesc = { 1, 0 };
	texDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
	texDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;

	HRESULT hr = m_pD3DDevice->CreateCommittedResource(
		&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT),
		D3D12_HEAP_FLAG_NONE,
		&texDesc,
		D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
		nullptr,
		IID_PPV_ARGS(&m_pOutputDiffuse));
	ASSERT(SUCCEEDED(hr), "Failed to create output diffuse texture resource.");

	m_pOutputDiffuse->SetName(L"CRayTracingManager::m_pOutputDiffuse");

	// Create UAV
	CD3DX12_CPU_DESCRIPTOR_HANDLE uavHandle(m_pCommonDescriptorHeap->GetCPUDescriptorHandleForHeapStart(), COMMON_DESCRIPTOR_INDEX_OUTPUT_DIFFUSE_UAV, m_DescriptorSize);
	m_pD3DDevice->CreateUnorderedAccessView(m_pOutputDiffuse, nullptr, nullptr, uavHandle);

	return true;
}

void RayTracingManager::cleanupOutputDiffuseBuffer()
{
	SAFE_RELEASE(m_pOutputDiffuse);
}

bool RayTracingManager::createOutputDepthBuffer(uint width, uint height)
{
	// Create Output Buffer, Texture, SRV
	D3D12_RESOURCE_DESC texDesc = {};
	texDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
	texDesc.Alignment = 0;
	texDesc.Width = width;
	texDesc.Height = height;
	texDesc.DepthOrArraySize = 1;
	texDesc.MipLevels = 1;
	texDesc.Format = DXGI_FORMAT_R32_TYPELESS;
	texDesc.SampleDesc = { 1, 0 };
	texDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
	texDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;

	HRESULT hr = m_pD3DDevice->CreateCommittedResource(
		&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT),
		D3D12_HEAP_FLAG_NONE,
		&texDesc,
		D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
		nullptr,
		IID_PPV_ARGS(&m_pOutputDepth));
	ASSERT(SUCCEEDED(hr), "Failed to create output depth texture resource.");
	m_pOutputDepth->SetName(L"CRayTracingManager::m_pOutputDepth");

	// Create UAV
	D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
	uavDesc.Format = DXGI_FORMAT_R32_FLOAT;
	uavDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
	uavDesc.Texture2D.MipSlice = 0;
	uavDesc.Texture2D.PlaneSlice = 0;
	uavDesc.Buffer.StructureByteStride = sizeof(float);
	uavDesc.Buffer.NumElements = width * height;
	uavDesc.Buffer.Flags = D3D12_BUFFER_UAV_FLAG_NONE;

	CD3DX12_CPU_DESCRIPTOR_HANDLE uavHandle(m_pCommonDescriptorHeap->GetCPUDescriptorHandleForHeapStart(), COMMON_DESCRIPTOR_INDEX_OUTPUT_DEPTH_UAV, m_DescriptorSize);
	m_pD3DDevice->CreateUnorderedAccessView(m_pOutputDepth, nullptr, &uavDesc, uavHandle);

	return true;
}

void RayTracingManager::cleanupOutputDepthBuffer()
{
	SAFE_RELEASE(m_pOutputDepth);
}

void RayTracingManager::createRaytracingPipelineStateObject()
{
	RootSignatureManager* pRootSignatureManager = m_pRenderer->GetRootSignatureManager();

	CD3DX12_STATE_OBJECT_DESC raytracingPipeline{ D3D12_STATE_OBJECT_TYPE_RAYTRACING_PIPELINE };
	CD3DX12_DXIL_LIBRARY_SUBOBJECT* pLibTri = raytracingPipeline.CreateSubobject<CD3DX12_DXIL_LIBRARY_SUBOBJECT>();
	D3D12_SHADER_BYTECODE libdxilTri = CD3DX12_SHADER_BYTECODE(m_pRayShader->CodeBuffer, m_pRayShader->CodeSize);

	pLibTri->SetDXILLibrary(&libdxilTri);
	pLibTri->DefineExport(g_RaygenShaderName);
	for (uint i = 0; i < NUM_RAYTRACING_SHADER_TYPES; ++i)
	{
		pLibTri->DefineExport(g_ClosestHitShaderNames[i]);
		pLibTri->DefineExport(g_AnyHitShaderNames[i]);
		pLibTri->DefineExport(g_MissShaderNames[i]);
	}

	CD3DX12_DXIL_LIBRARY_SUBOBJECT* pLibProc = raytracingPipeline.CreateSubobject<CD3DX12_DXIL_LIBRARY_SUBOBJECT>();
	D3D12_SHADER_BYTECODE libdxilProc = CD3DX12_SHADER_BYTECODE(m_pRayProcShader->CodeBuffer, m_pRayProcShader->CodeSize);

	pLibProc->SetDXILLibrary(&libdxilProc);
	pLibProc->DefineExport(g_IntersectionShaderName_Proc);
	for (uint i = 0; i < NUM_RAYTRACING_SHADER_TYPES; ++i)
	{
		pLibProc->DefineExport(g_ClosestHitShaderNames_Proc[i]);
	}

	CD3DX12_DXIL_LIBRARY_SUBOBJECT* pLibCasper = raytracingPipeline.CreateSubobject<CD3DX12_DXIL_LIBRARY_SUBOBJECT>();
	D3D12_SHADER_BYTECODE libdxilCasper = CD3DX12_SHADER_BYTECODE(m_pRayCasperShader->CodeBuffer, m_pRayCasperShader->CodeSize);

	pLibCasper->SetDXILLibrary(&libdxilCasper);
	pLibCasper->DefineExport(g_IntersectionShaderName_Casper);
	for (uint i = 0; i < NUM_RAYTRACING_SHADER_TYPES; ++i)
	{
		pLibCasper->DefineExport(g_ClosestHitShaderNames_Casper[i]);
	}

	// Triangle hitgroups (closest + any)
	for (uint i = 0; i < NUM_RAYTRACING_SHADER_TYPES; ++i)
	{
		CD3DX12_HIT_GROUP_SUBOBJECT* pHitGroup = raytracingPipeline.CreateSubobject<CD3DX12_HIT_GROUP_SUBOBJECT>();
		pHitGroup->SetClosestHitShaderImport(g_ClosestHitShaderNames[i]);
		pHitGroup->SetAnyHitShaderImport(g_AnyHitShaderNames[i]);
		pHitGroup->SetHitGroupExport(g_HitGroupNames[i]);
		pHitGroup->SetHitGroupType(D3D12_HIT_GROUP_TYPE_TRIANGLES);
	}
	// Procedural hitgroups (intersection + closest)
	for (uint i = 0; i < NUM_RAYTRACING_SHADER_TYPES; ++i)
	{
		CD3DX12_HIT_GROUP_SUBOBJECT* pHitGroup = raytracingPipeline.CreateSubobject<CD3DX12_HIT_GROUP_SUBOBJECT>();
		pHitGroup->SetClosestHitShaderImport(g_ClosestHitShaderNames_Proc[i]);
		pHitGroup->SetIntersectionShaderImport(g_IntersectionShaderName_Proc);
		pHitGroup->SetHitGroupExport(g_HitGroupNames_Proc[i]);
		pHitGroup->SetHitGroupType(D3D12_HIT_GROUP_TYPE_PROCEDURAL_PRIMITIVE);
	}
	// Casper hitgroups (intersection + closest)
	for (uint i = 0; i < NUM_RAYTRACING_SHADER_TYPES; ++i)
	{
		CD3DX12_HIT_GROUP_SUBOBJECT* pHitGroup = raytracingPipeline.CreateSubobject<CD3DX12_HIT_GROUP_SUBOBJECT>();
		pHitGroup->SetClosestHitShaderImport(g_ClosestHitShaderNames_Casper[i]);
		pHitGroup->SetIntersectionShaderImport(g_IntersectionShaderName_Casper);
		pHitGroup->SetHitGroupExport(g_HitGroupNames_Casper[i]);
		pHitGroup->SetHitGroupType(D3D12_HIT_GROUP_TYPE_PROCEDURAL_PRIMITIVE);
	}

	CD3DX12_RAYTRACING_SHADER_CONFIG_SUBOBJECT* pShaderConfig = raytracingPipeline.CreateSubobject<CD3DX12_RAYTRACING_SHADER_CONFIG_SUBOBJECT>();
	UINT payloadSize = PAYLOAD_SIZE;
	UINT attributeSize = 24;
	pShaderConfig->Config(payloadSize, attributeSize);

	CD3DX12_LOCAL_ROOT_SIGNATURE_SUBOBJECT* pLocalRootSignature = raytracingPipeline.CreateSubobject<CD3DX12_LOCAL_ROOT_SIGNATURE_SUBOBJECT>();
	pLocalRootSignature->SetRootSignature(pRootSignatureManager->Query(ERootSignatureType::GraphicsRaytracingLocal));
	CD3DX12_SUBOBJECT_TO_EXPORTS_ASSOCIATION_SUBOBJECT* pRootSignatureAssociation = raytracingPipeline.CreateSubobject<CD3DX12_SUBOBJECT_TO_EXPORTS_ASSOCIATION_SUBOBJECT>();
	pRootSignatureAssociation->SetSubobjectToAssociate(*pLocalRootSignature);

	pRootSignatureAssociation->AddExports(g_HitGroupNames); // Tri
	pRootSignatureAssociation->AddExports(g_HitGroupNames_Proc);  // Proc
	pRootSignatureAssociation->AddExports(g_HitGroupNames_Casper);  // Casper

	CD3DX12_GLOBAL_ROOT_SIGNATURE_SUBOBJECT* pGlobalRootSignature = raytracingPipeline.CreateSubobject<CD3DX12_GLOBAL_ROOT_SIGNATURE_SUBOBJECT>();
	pGlobalRootSignature->SetRootSignature(pRootSignatureManager->Query(ERootSignatureType::GraphicsRaytracingGlobal));

	CD3DX12_RAYTRACING_PIPELINE_CONFIG_SUBOBJECT* pPipelineConfig = raytracingPipeline.CreateSubobject<CD3DX12_RAYTRACING_PIPELINE_CONFIG_SUBOBJECT>();
	// PERFOMANCE TIP: Set max recursion depth as low as needed 
	// as drivers may apply optimization strategies for low recursion depths. 
	UINT maxRecursionDepth = MAX_RECURSION_DEPTH; // ~ primary rays only. 
	pPipelineConfig->Config(maxRecursionDepth);

	// Create the state object.
	const D3D12_STATE_OBJECT_DESC* pRaytracingPipeline = raytracingPipeline;
	HRESULT hr = m_pD3DDevice->CreateStateObject(pRaytracingPipeline, IID_PPV_ARGS(&m_pDXRStateObject));
	ASSERT(SUCCEEDED(hr), "Failed to create raytracing pipeline state object.");

	m_pDXRStateObject->SetName(L"CRayTracingManager::m_pDXRStateObject");
}

void RayTracingManager::buildShaderTables()
{
	// Get shader identifiers.
	ID3D12StateObjectProperties* pStateObjectProperties = nullptr;
	m_pDXRStateObject->QueryInterface(IID_PPV_ARGS(&pStateObjectProperties));

	void* pRayGenShaderIdentifier = pStateObjectProperties->GetShaderIdentifier(g_RaygenShaderName);

	// Raygen shader table
	ShaderRecord rayGenShaderRecord = ShaderRecord(pRayGenShaderIdentifier, m_ShaderIdentifierSize, nullptr, 0);
	m_pRayGenShaderTable = new ShaderTable;
	m_pRayGenShaderTable->Initiailze(m_pD3DDevice, m_ShaderIdentifierSize, L"RayGenShaderTable");
	m_pRayGenShaderTable->CommitResource(1);
	m_pRayGenShaderTable->InsertShaderRecord(&rayGenShaderRecord);

	// Miss shader table
	m_pMissShaderTable = new ShaderTable;
	m_pMissShaderTable->Initiailze(m_pD3DDevice, m_ShaderIdentifierSize, L"MissShaderTable");
	m_pMissShaderTable->CommitResource(NUM_RAYTRACING_SHADER_TYPES);
	for (int i = 0; i < NUM_RAYTRACING_SHADER_TYPES; ++i)
	{
		void* pMissShaderIdentifier = pStateObjectProperties->GetShaderIdentifier(g_MissShaderNames[i]);
		ShaderRecord missShaderRecord = ShaderRecord(pMissShaderIdentifier, m_ShaderIdentifierSize);
		m_pMissShaderTable->InsertShaderRecord(&missShaderRecord);
	}
	m_MissShaderTableStrideInBytes = m_pMissShaderTable->GetShaderRecordSize();

	// Hitgroup Shader Table
	m_pHitGroupShaderTable = new ShaderTable;
	m_pHitGroupShaderTable->Initiailze(m_pD3DDevice, m_ShaderIdentifierSize + sizeof(RootArgument), L"HitGroupShaderTable");
	m_HitGroupShaderRecordSize = m_pHitGroupShaderTable->GetShaderRecordSize();

	SAFE_RELEASE(pStateObjectProperties);
}

void RayTracingManager::cleanupShaderTables()
{
	if (m_pRayGenShaderTable)
	{
		delete m_pRayGenShaderTable;
		m_pRayGenShaderTable = nullptr;
	}
	if (m_pMissShaderTable)
	{
		delete m_pMissShaderTable;
		m_pMissShaderTable = nullptr;
	}
	if (m_pHitGroupShaderTable)
	{
		delete m_pHitGroupShaderTable;
		m_pHitGroupShaderTable = nullptr;
	}
}

void RayTracingManager::createDescriptorHeapCBV_SRV_UAV()
{
	D3D12_DESCRIPTOR_HEAP_DESC srvHeapDesc = {};
	srvHeapDesc.NumDescriptors = COMMON_DESCRIPTOR_COUNT;
	srvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
	srvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;

	HRESULT hr = m_pD3DDevice->CreateDescriptorHeap(&srvHeapDesc, IID_PPV_ARGS(&m_pCommonDescriptorHeap));
	ASSERT(SUCCEEDED(hr), "Failed to create descriptor heap for CBV_SRV_UAV.");

	m_pCommonDescriptorHeap->SetName(L"CD3D12Renderer::m_pCommonDescriptorHeap");

	m_DescriptorSize = m_pD3DDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
}

void RayTracingManager::cleanupDescriptorHeapCBV_SRV_UAV()
{
	SAFE_RELEASE(m_pCommonDescriptorHeap);
}

void RayTracingManager::createShaderVisibleHeap(uint maxNumDescriptors)
{
	D3D12_DESCRIPTOR_HEAP_DESC heapDesc = {};
	heapDesc.NumDescriptors = (UINT)maxNumDescriptors;
	heapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
	heapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;

	HRESULT hr = m_pD3DDevice->CreateDescriptorHeap(&heapDesc, IID_PPV_ARGS(&m_pShaderVisibleDescriptorHeap));
	ASSERT(SUCCEEDED(hr), "Failed to create shader visible descriptor heap.");
}

void RayTracingManager::cleanupDispatchHeap()
{
	SAFE_RELEASE(m_pShaderVisibleDescriptorHeap);
}