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

// Hit group
const wchar_t* g_HitGroupNames[NUM_RAYTRACING_SHADER_TYPES] =
{
	L"MyHitGroup_Triangle_RadianceRay",
	L"MyHitGroup_Triangle_ShadowRay"
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

	createOutputDiffuseBuffer(m_Width, m_Height);
	createOutputDepthBuffer(m_Width, m_Height);

	createRootSignatures();
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
	SAFE_RELEASE(m_pRaytracingGlobalRootSignature);
	SAFE_RELEASE(m_pRaytracingLocalRootSignature);
	SAFE_CLEANUP(m_pRayShader, pShaderManager->ReleaseShader);

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
	CD3DX12_CPU_DESCRIPTOR_HANDLE	dispatchHeapHandleCPU(m_pShaderVisibleDescriptorHeap->GetCPUDescriptorHandleForHeapStart());

	D3D12_CPU_DESCRIPTOR_HANDLE	cbvHandle = {};
	SimpleConstantBufferPool* pConstantBufferPool = m_pRenderer->GetConstantBufferPool(CONSTANT_BUFFER_TYPE_PER_FRAME, 0);
	SimpleConstantBufferPool* pConstantBufferPoolAtmos = m_pRenderer->GetConstantBufferPool(CONSTANT_BUFFER_TYPE_ATMOS, 0);
	ConstantBufferContainer* pCB = pConstantBufferPool->Alloc();
	ConstantBufferContainer* pCBAtmos = pConstantBufferPoolAtmos->Alloc();
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

	// (0) CBV - RayTracing
	m_pD3DDevice->CopyDescriptorsSimple(1, dispatchHeapHandleCPU, pCB->CBVHandle, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
	dispatchHeapHandleCPU.Offset(1, m_DescriptorSize);

	// (1) CBV - Atmosphere
	m_pD3DDevice->CopyDescriptorsSimple(1, dispatchHeapHandleCPU, pCBAtmos->CBVHandle, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
	dispatchHeapHandleCPU.Offset(1, m_DescriptorSize);

	// (2) UAV - output diffuse
	CD3DX12_CPU_DESCRIPTOR_HANDLE uavDiffuse(m_pCommonDescriptorHeap->GetCPUDescriptorHandleForHeapStart(), COMMON_DESCRIPTOR_INDEX_OUTPUT_DIFFUSE_UAV, m_DescriptorSize);
	m_pD3DDevice->CopyDescriptorsSimple(1, dispatchHeapHandleCPU, uavDiffuse, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
	dispatchHeapHandleCPU.Offset(1, m_DescriptorSize);

	// (3) UAV - output depth
	CD3DX12_CPU_DESCRIPTOR_HANDLE uavDepth(m_pCommonDescriptorHeap->GetCPUDescriptorHandleForHeapStart(), COMMON_DESCRIPTOR_INDEX_OUTPUT_DEPTH_UAV, m_DescriptorSize);
	m_pD3DDevice->CopyDescriptorsSimple(1, dispatchHeapHandleCPU, uavDepth, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
	dispatchHeapHandleCPU.Offset(1, m_DescriptorSize);

	// (4) SRV - Sky Texture
	const TextureHandle* skyTransmittanceTexture = m_pRenderer->GetSkyTransmittanceTexture();
	m_pD3DDevice->CopyDescriptorsSimple(1, dispatchHeapHandleCPU, skyTransmittanceTexture->SRV, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
	dispatchHeapHandleCPU.Offset(1, m_DescriptorSize);

	const TextureHandle* skyScatteringTexture = m_pRenderer->GetSkyScatteringTexture();
	m_pD3DDevice->CopyDescriptorsSimple(1, dispatchHeapHandleCPU, skyScatteringTexture->SRV, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
	dispatchHeapHandleCPU.Offset(1, m_DescriptorSize);

	const TextureHandle* skyIrradianceTexture = m_pRenderer->GetSkyIrradianceTexture();
	m_pD3DDevice->CopyDescriptorsSimple(1, dispatchHeapHandleCPU, skyIrradianceTexture->SRV, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
	dispatchHeapHandleCPU.Offset(1, m_DescriptorSize);

	CD3DX12_RESOURCE_BARRIER rcBarrier[] =
	{
		CD3DX12_RESOURCE_BARRIER::Transition(m_pOutputDiffuse, D3D12_RESOURCE_STATE_ALL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS),
		CD3DX12_RESOURCE_BARRIER::Transition(m_pOutputDepth, D3D12_RESOURCE_STATE_ALL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS),
	};
	pCommandList->ResourceBarrier((UINT)_countof(rcBarrier), rcBarrier);

	pCommandList->SetComputeRootSignature(m_pRaytracingGlobalRootSignature);

	// Bind the heaps, acceleration structure and dispatch rays.    
	D3D12_DISPATCH_RAYS_DESC dispatchDesc = {};
	ID3D12DescriptorHeap* ppHeaps[] = { m_pShaderVisibleDescriptorHeap };
	pCommandList->SetDescriptorHeaps(_countof(ppHeaps), ppHeaps);

	CD3DX12_GPU_DESCRIPTOR_HANDLE dispatchHeapHandleGPU(m_pShaderVisibleDescriptorHeap->GetGPUDescriptorHandleForHeapStart());
	pCommandList->SetComputeRootDescriptorTable(0, dispatchHeapHandleGPU);
	pCommandList->SetComputeRootShaderResourceView(1, m_pTLAS->GetGPUVirtualAddress());

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

	CD3DX12_RESOURCE_BARRIER rcBarrierInv[] =
	{
		CD3DX12_RESOURCE_BARRIER::Transition(m_pOutputDiffuse, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_ALL_SHADER_RESOURCE),
		CD3DX12_RESOURCE_BARRIER::Transition(m_pOutputDepth, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_ALL_SHADER_RESOURCE)
	};
	pCommandList->ResourceBarrier((UINT)_countof(rcBarrierInv), rcBarrierInv);

	// Clear BLAS instance lists
	for (uint i = 0; i < MAX_RENDER_THREAD_COUNT; ++i)
	{
		m_BLASInstanceListThisFrame[i].clear();
	}
}

BLASHandle* RayTracingManager::AllocBLAS(
	ID3D12Resource* pVertexBuffer,
	uint vertexSize,
	uint numVertices,
	const IndexedTriGroup* pTriGroupInfoList,
	uint numTriGroupInfos,
	bool bAllowUpdate)
{
	BLASHandle* pBLASHandle = nullptr;
	ID3D12Device5* pD3DDevice = m_pRenderer->GetD3DDevice();

	if (m_GlobalBLASHandleList.size() >= m_MaxNumBLASs)
	{
		ASSERT(false, "Exceeded maximum number of BLAS instances.");
		goto lb_return;
	}

	ASSERT(numTriGroupInfos < MAX_TRIGROUP_COUNT_PER_BLAS, "Too many triangle groups in BLAS");

	uint32_t index = m_pIndexCreator->Alloc();
	ASSERT(index != -1, "Failed to allocate index for BLAS instance.");

	// Create BLAS Handle
	pBLASHandle = new BLASHandle;
	pBLASHandle->pBLAS = nullptr; // BLAS는 아직 생성되지 않음.
	pBLASHandle->ID = index;
	pBLASHandle->bAllowUpdate = bAllowUpdate;
	pBLASHandle->ShaderRecordIndex = std::numeric_limits<uint32_t>::max(); // 아직 할당되지 않음.
	pBLASHandle->NumVertices = numVertices;
	pBLASHandle->NumTriGroups = numTriGroupInfos;

	// Fill Geometry Descriptions
	D3D12_RAYTRACING_GEOMETRY_DESC* pGeomDescList = pBLASHandle->pGeomDescList;
	D3D12_GPU_VIRTUAL_ADDRESS VB_GPU_Ptr = pVertexBuffer->GetGPUVirtualAddress();
	for (uint i = 0; i < numTriGroupInfos; ++i)
	{
		D3D12_GPU_VIRTUAL_ADDRESS IB_GPU_Ptr = pTriGroupInfoList[i].IndexBuffer->GetGPUVirtualAddress();

		pGeomDescList[i].Type = D3D12_RAYTRACING_GEOMETRY_TYPE_TRIANGLES;
		pGeomDescList[i].Triangles.IndexBuffer = IB_GPU_Ptr;
		pGeomDescList[i].Triangles.IndexCount = pTriGroupInfoList[i].NumTriangles * 3;
		pGeomDescList[i].Triangles.IndexFormat = DXGI_FORMAT_R16_UINT;
		pGeomDescList[i].Triangles.Transform3x4 = 0;
		pGeomDescList[i].Triangles.VertexFormat = DXGI_FORMAT_R32G32B32_FLOAT;
		pGeomDescList[i].Triangles.VertexCount = numVertices;
		pGeomDescList[i].Triangles.VertexBuffer.StartAddress = VB_GPU_Ptr;
		pGeomDescList[i].Triangles.VertexBuffer.StrideInBytes = vertexSize;
		// Mark the geometry as opaque. 
		// PERFORMANCE TIP: mark geometry as opaque whenever applicable as it can enable important ray processing optimizations.
		// Note: When rays encounter opaque geometry an any hit shader will not be executed whether it is present or not.
		// pGeomDescList[i].Flags = pTriGroupInfoList[i].bOpaque ? D3D12_RAYTRACING_GEOMETRY_FLAG_OPAQUE : D3D12_RAYTRACING_GEOMETRY_FLAG_NONE;
		bool bOpaque = pTriGroupInfoList[i].Material.Opacity > Material::OPACITY_THRESHOLD;
		pGeomDescList[i].Flags = bOpaque ? D3D12_RAYTRACING_GEOMETRY_FLAG_OPAQUE : D3D12_RAYTRACING_GEOMETRY_FLAG_NONE;
	}

	// Set Local Root Parameters
	{
		pBLASHandle->RootArgArray.resize(numTriGroupInfos);

		UINT descriptorIndex = DISPATCH_DESCRIPTOR_INDEX_COUNT + (LOCAL_ROOT_PARAM_DESCRIPTOR_COUNT * pBLASHandle->ID * MAX_TRIGROUP_COUNT_PER_BLAS);
		CD3DX12_CPU_DESCRIPTOR_HANDLE srvCpu(m_pShaderVisibleDescriptorHeap->GetCPUDescriptorHandleForHeapStart(), descriptorIndex, m_DescriptorSize);
		CD3DX12_GPU_DESCRIPTOR_HANDLE srvGpu(m_pShaderVisibleDescriptorHeap->GetGPUDescriptorHandleForHeapStart(), descriptorIndex, m_DescriptorSize);

		D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
		srvDesc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
		srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;

		for (uint i = 0; i < numTriGroupInfos; ++i)
		{
			// Set Material
			pBLASHandle->RootArgArray[i].Cb.Material = pTriGroupInfoList[i].Material;

			// Create Shader Resource from Vertex Buffer
			srvDesc.Buffer.FirstElement = 0;
			srvDesc.Buffer.NumElements = numVertices;
			srvDesc.Format = DXGI_FORMAT_UNKNOWN;
			srvDesc.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_NONE;
			srvDesc.Buffer.StructureByteStride = vertexSize;

			pD3DDevice->CreateShaderResourceView(pVertexBuffer, &srvDesc, srvCpu);
			pBLASHandle->RootArgArray[i].SrvVB = srvGpu;
			srvCpu.Offset(1, m_DescriptorSize);
			srvGpu.Offset(1, m_DescriptorSize);

			// Create Shader Resource from Index Buffer			
			srvDesc.Buffer.FirstElement = 0;
			srvDesc.Buffer.NumElements = (pTriGroupInfoList[i].NumTriangles * 3 * 2) / 4;	// compute shader에서 4bytes 단위로 읽어야 하므로...
			srvDesc.Format = DXGI_FORMAT_R32_TYPELESS;
			srvDesc.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_RAW;
			srvDesc.Buffer.StructureByteStride = 0;

			m_pD3DDevice->CreateShaderResourceView(pTriGroupInfoList[i].IndexBuffer, &srvDesc, srvCpu);
			pBLASHandle->RootArgArray[i].SrvIB = srvGpu;
			srvCpu.Offset(1, m_DescriptorSize);
			srvGpu.Offset(1, m_DescriptorSize);

			// Diffuse Texture
			if (pTriGroupInfoList[i].DiffuseTexHandle)
			{
				D3D12_CPU_DESCRIPTOR_HANDLE srvTexSrc = pTriGroupInfoList[i].DiffuseTexHandle->SRV;
				if (srvTexSrc.ptr)
				{
					pD3DDevice->CopyDescriptorsSimple(1, srvCpu, srvTexSrc, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
				}
			}
			pBLASHandle->RootArgArray[i].SrvTexDiffuse = srvGpu;
			srvCpu.Offset(1, m_DescriptorSize);
			srvGpu.Offset(1, m_DescriptorSize);

			// Normal Texture
			if (pTriGroupInfoList[i].NormalTexHandle)
			{
				D3D12_CPU_DESCRIPTOR_HANDLE srvTexSrc = pTriGroupInfoList[i].NormalTexHandle->SRV;
				if (srvTexSrc.ptr)
				{
					pD3DDevice->CopyDescriptorsSimple(1, srvCpu, srvTexSrc, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
				}
			}
			pBLASHandle->RootArgArray[i].SrvTexNormal = srvGpu;
			srvCpu.Offset(1, m_DescriptorSize);
			srvGpu.Offset(1, m_DescriptorSize);
		}
	}

	m_GlobalBLASHandleList.emplace_back(pBLASHandle);
	m_UpdateAccelerationStructureFlags = UPDATE_ACCELERATION_STRCTURE_TYPE_HIT_GROUP_SHADER_TABLE | UPDATE_ACCELERATION_STRCTURE_TYPE_TLAS;
lb_return:
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

ID3D12Resource* RayTracingManager::buildTLAS(ID3D12GraphicsCommandList6* pCommandList,bool bAllowUpdate,uint currContextIndex)
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
	void* pHitGroupShaderIdentifier[NUM_RAYTRACING_SHADER_TYPES] = {};
	for (uint i = 0; i < NUM_RAYTRACING_SHADER_TYPES; ++i)
	{
		pHitGroupShaderIdentifier[i] = pStateObjectProperties->GetShaderIdentifier(g_HitGroupNames[i]);
		ASSERT(pHitGroupShaderIdentifier[i], "Failed to get hit group shader identifier.");
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
				ShaderRecord record = ShaderRecord(pHitGroupShaderIdentifier[j], m_ShaderIdentifierSize, &curr->RootArgArray[i], sizeof(RootArgument));
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
	texDesc.MipLevels = 1;
	texDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	texDesc.Width = width;
	texDesc.Height = height;
	texDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
	texDesc.DepthOrArraySize = 1;
	texDesc.SampleDesc.Count = 1;
	texDesc.SampleDesc.Quality = 0;
	texDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;

	HRESULT hr = m_pD3DDevice->CreateCommittedResource(
		&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT),
		D3D12_HEAP_FLAG_NONE,
		&texDesc,
		D3D12_RESOURCE_STATE_ALL_SHADER_RESOURCE,
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
	texDesc.MipLevels = 1;
	texDesc.Format = DXGI_FORMAT_R32_TYPELESS;
	texDesc.Width = width;
	texDesc.Height = height;
	texDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
	//texDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
	texDesc.DepthOrArraySize = 1;
	texDesc.SampleDesc.Count = 1;
	texDesc.SampleDesc.Quality = 0;
	texDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;

	HRESULT hr = m_pD3DDevice->CreateCommittedResource(
		&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT),
		D3D12_HEAP_FLAG_NONE,
		&texDesc,
		D3D12_RESOURCE_STATE_ALL_SHADER_RESOURCE,
		nullptr,
		IID_PPV_ARGS(&m_pOutputDepth));
	ASSERT(SUCCEEDED(hr), "Failed to create output depth texture resource.");

	m_pOutputDepth->SetName(L"CRayTracingManager::m_pOutputDepth");

	// Create UAV
	D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
	uavDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
	uavDesc.Buffer.StructureByteStride = sizeof(float);
	uavDesc.Buffer.NumElements = width * height;
	uavDesc.Format = DXGI_FORMAT_R32_FLOAT;
	uavDesc.Buffer.Flags = D3D12_BUFFER_UAV_FLAG_NONE;

	CD3DX12_CPU_DESCRIPTOR_HANDLE uavHandle(m_pCommonDescriptorHeap->GetCPUDescriptorHandleForHeapStart(), COMMON_DESCRIPTOR_INDEX_OUTPUT_DEPTH_UAV, m_DescriptorSize);
	m_pD3DDevice->CreateUnorderedAccessView(m_pOutputDepth, nullptr, &uavDesc, uavHandle);

	return true;
}

void RayTracingManager::cleanupOutputDepthBuffer()
{
	SAFE_RELEASE(m_pOutputDepth);
}

void RayTracingManager::createRootSignatures()
{
	// Global Root Signature
	// This is a root signature that is shared across all raytracing shaders invoked during a DispatchRays() call.

	// root param 0
	// output-diffuse(uav) | output-depth(uav)

	// root param 1
	// Acceleration Sturecture

	CD3DX12_DESCRIPTOR_RANGE globalRanges[4] = {};
	globalRanges[0].Init(D3D12_DESCRIPTOR_RANGE_TYPE_CBV, 1, /*b*/0, /*space*/0); // b0, space0
	globalRanges[1].Init(D3D12_DESCRIPTOR_RANGE_TYPE_CBV, 1, /*b*/0, /*space*/1); // b0, space1
	globalRanges[2].Init(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 2, /*u*/0, /*space*/0); // u0 : u0-diffuse | u1 : out-depth
	globalRanges[3].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 3, /*t*/11, /*space*/0); // t0 : AccelerationStructure

	// b0 : RaytracingCBV | u0 : u0-diffuse | u1 : out-depth | t0 : AccelerationStructure
	CD3DX12_ROOT_PARAMETER globalRootParameters[2] = {};
	globalRootParameters[0].InitAsDescriptorTable(_countof(globalRanges), globalRanges, D3D12_SHADER_VISIBILITY_ALL);
	globalRootParameters[1].InitAsShaderResourceView(0);	// Acceleration Structure

	// sampler
	D3D12_STATIC_SAMPLER_DESC samplers[4] = {};
	D3DUtil::SetSamplerDesc_Wrap(samplers + 0, 0);	// Wrap Linear
	D3DUtil::SetSamplerDesc_Clamp(samplers + 1, 1);	// Clamp Linear
	D3DUtil::SetSamplerDesc_Wrap(samplers + 2, 2);	// Wrap Point
	samplers[2].Filter = D3D12_FILTER_MIN_MAG_MIP_POINT;
	D3DUtil::SetSamplerDesc_Mirror(samplers + 3, 3);	// Mirror Linear
	samplers[3].Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
	for (uint i = 0; i < (uint)_countof(samplers); ++i)
	{
		samplers[i].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
	}
	CD3DX12_ROOT_SIGNATURE_DESC globalRootSignatureDesc(ARRAYSIZE(globalRootParameters), globalRootParameters, (DWORD)_countof(samplers), samplers);
	D3DUtil::SerializeAndCreateRaytracingRootSignature(m_pD3DDevice, &globalRootSignatureDesc, &m_pRaytracingGlobalRootSignature);

	// Local Root Signature
	// space1
	// t0 : vertex buffer, t1 : index buffer, t2 : diffuse texture, t3 : normal texture
	CD3DX12_DESCRIPTOR_RANGE localRanges[1] = {};
	localRanges[0].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 4, 0, 1);	// space1

	CD3DX12_ROOT_PARAMETER localRootParameters[2] = {};
	localRootParameters[0].InitAsConstants(SizeOfInUint32(CONSTANT_BUFFER_RT_TRIGROUP), /*b*/1, /*space*/0, D3D12_SHADER_VISIBILITY_ALL);
	localRootParameters[1].InitAsDescriptorTable(_countof(localRanges), localRanges, D3D12_SHADER_VISIBILITY_ALL);

	CD3DX12_ROOT_SIGNATURE_DESC localRootSignatureDesc(ARRAYSIZE(localRootParameters), localRootParameters, 0, nullptr);
	localRootSignatureDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_LOCAL_ROOT_SIGNATURE;
	D3DUtil::SerializeAndCreateRaytracingRootSignature(m_pD3DDevice, &localRootSignatureDesc, &m_pRaytracingLocalRootSignature);
}

void RayTracingManager::createRaytracingPipelineStateObject()
{
	// 총 7개의 Subobject를 생성하여 RTPSO(Ray Tracing Pipeline State Object)를 구성
	// Subobject는 각각의 DXIL export(즉, 쉐이더 엔트리 포인트)에 기본 또는 명시적 방식으로 연결됨

	// 구성:
	// 1 - DXIL(DirectX Intermediate Language) library
	// 1 - Triangle hit group
	// 1 - Shader config (payload, attribute 크기)
	// 2 - Local root signature and association
	// 1 - Global root signature
	// 1 - Pipeline config (재귀 깊이 등)
	CD3DX12_STATE_OBJECT_DESC raytracingPipeline{ D3D12_STATE_OBJECT_TYPE_RAYTRACING_PIPELINE };


	// 1) DXIL 라이브러리 Subobject 생성
	// 셰이더는 서브오브젝트로 간주되지 않으므로 DXIL 라이브러리를 통해서 전달되어야 한다.
	// DXIL library
	// This contains the shaders and their entrypoints for the state object.
	// Since shaders are not considered a subobject, they need to be passed in via DXIL library subobjects.
	CD3DX12_DXIL_LIBRARY_SUBOBJECT* pLib = raytracingPipeline.CreateSubobject<CD3DX12_DXIL_LIBRARY_SUBOBJECT>();

	// Shader Bytecode 설정 (컴파일된 DXIL)
	D3D12_SHADER_BYTECODE libdxil = CD3DX12_SHADER_BYTECODE(m_pRayShader->CodeBuffer, m_pRayShader->CodeSize);
	pLib->SetDXILLibrary(&libdxil);

	// DXIL 라이브러리에서 사용할 쉐이더 export들을 정의
	pLib->DefineExport(g_RaygenShaderName);

	// HitGroup에서 import할 수 있도록 export
	// 쉐이더 타입별(radiance/shadow)로 Closest Hit, Any Hit, Miss 쉐이더를 export
	for (uint i = 0; i < NUM_RAYTRACING_SHADER_TYPES; ++i)
	{
		pLib->DefineExport(g_ClosestHitShaderNames[i]);	// hit group에서 import할 수 있도록 export
		pLib->DefineExport(g_AnyHitShaderNames[i]);
		pLib->DefineExport(g_MissShaderNames[i]);
	}

	// 2) Triangle hit group
	// 히트 그룹 Subobject 생성
	// 히트 그룹은 Geometry에 레이가 교차했을 때 실행할 ClosestHit, AnyHit, Intersection 쉐이더를 정의
	for (uint i = 0; i < NUM_RAYTRACING_SHADER_TYPES; ++i)
	{
		CD3DX12_HIT_GROUP_SUBOBJECT* pHitGroup = raytracingPipeline.CreateSubobject<CD3DX12_HIT_GROUP_SUBOBJECT>();
		pHitGroup->SetClosestHitShaderImport(g_ClosestHitShaderNames[i]);
		pHitGroup->SetAnyHitShaderImport(g_AnyHitShaderNames[i]);
		pHitGroup->SetHitGroupExport(g_HitGroupNames[i]);
	}
	//pHitGroup->SetIntersectionShaderImport(); <- trinagle만 처리하므로 필요없다.

	// 3) Shader config
	// Defines the maximum sizes in bytes for the ray payload and attribute structure.
	// Payload와 Attribute 구조의 최대 크기를 설정
	CD3DX12_RAYTRACING_SHADER_CONFIG_SUBOBJECT* pShaderConfig = raytracingPipeline.CreateSubobject<CD3DX12_RAYTRACING_SHADER_CONFIG_SUBOBJECT>();
	UINT payloadSize = PAYLOAD_SIZE;
	UINT attributeSize = 2 * sizeof(float); // float2 barycentrics
	pShaderConfig->Config(payloadSize, attributeSize);

	// 4,5) Local root signature and shader association
	// Local Root Signature 및 연결 설정 (명시적 연결 사용)
	// Shader Table에서 각 쉐이더가 고유한 인자를 받을 수 있도록 해줌
	//
	// Raytracing Pipeline State Object에 Local Root Signature 서브오브젝트를 추가
	//
	CD3DX12_LOCAL_ROOT_SIGNATURE_SUBOBJECT* pLocalRootSignature = raytracingPipeline.CreateSubobject<CD3DX12_LOCAL_ROOT_SIGNATURE_SUBOBJECT>();
	pLocalRootSignature->SetRootSignature(m_pRaytracingLocalRootSignature);
	CD3DX12_SUBOBJECT_TO_EXPORTS_ASSOCIATION_SUBOBJECT* pRootSignatureAssociation = raytracingPipeline.CreateSubobject<CD3DX12_SUBOBJECT_TO_EXPORTS_ASSOCIATION_SUBOBJECT>();
	pRootSignatureAssociation->SetSubobjectToAssociate(*pLocalRootSignature);
	pRootSignatureAssociation->AddExports(g_HitGroupNames);

	// 6) Global root signature
	// Global Root Signature Subobject 생성
	// DispatchRays() 호출 중 모든 쉐이더가 볼 수 있는 루트 시그니처
	CD3DX12_GLOBAL_ROOT_SIGNATURE_SUBOBJECT* pGlobalRootSignature = raytracingPipeline.CreateSubobject<CD3DX12_GLOBAL_ROOT_SIGNATURE_SUBOBJECT>();
	pGlobalRootSignature->SetRootSignature(m_pRaytracingGlobalRootSignature);

	// 7) Pipeline config
	// TraceRay() 함수의 최대 재귀 깊이를 설정
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