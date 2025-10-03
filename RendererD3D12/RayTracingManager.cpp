#include "pch.h"
#include "D3D12ResourceManager.h"
#include "SimpleConstantBufferPool.h"
#include "ConstantBufferManager.h"
#include "ShaderManager.h"
#include "ShaderRecord.h"
#include "D3D12Renderer.h"
#include "ShaderTable.h"
#include "RayTracingManager.h"

const wchar_t* g_RaygenShaderName = { L"MyRaygenShader_RadianceRay" };
const wchar_t* g_ClosestHitShaderName[] = { L"MyClosestHitShader_RadianceRay" };
const wchar_t* g_MissShaderName[] = { L"MyMissShader_RadianceRay" };
const wchar_t* g_AnyHitShaderName[] = { L"MyAnyHitShader_RadianceRay" };

// Hit groups.
const wchar_t* g_HitGroupName[] = { L"MyHitGroup_Triangle_RadianceRay" };

bool RayTracingManager::Initialize(D3D12Renderer* pRenderer, UINT width, UINT height)
{
	HRESULT hr = S_OK;

	m_pRenderer = pRenderer;
	m_pD3DDevice = pRenderer->GetD3DDevice();
	ShaderManager* pShaderManager = pRenderer->GetShaderManager();

	D3D12_COMMAND_QUEUE_DESC queueDesc = {};
	queueDesc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
	queueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;

	hr = m_pD3DDevice->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&m_pCommandQueue));
	ASSERT(SUCCEEDED(hr), "Failed to create command queue.");

	createCommandList();
	createFence();

	m_Width = width;
	m_Height = height;

	createDescriptorHeapCBV_SRV_UAV();
	createShaderVisibleHeap();

	m_pRayShader = pShaderManager->CreateShaderDXC(L"Raytracing.hlsl", L"", L"lib_6_3", 0);

	createOutputDiffuseBuffer(m_Width, m_Height);
	createOutputDepthBuffer(m_Width, m_Height);

	createRootSignatures();
	createRaytracingPipelineStateObject();

	// build geometry
	initMesh();

	// build acceleration structure
	initAccelerationStructure();

	return true;
}

void RayTracingManager::Cleanup()
{
	ShaderManager* pShaderManager = m_pRenderer->GetShaderManager();

	if (m_pCommandQueue)
	{
		m_pCommandQueue->Release();
		m_pCommandQueue = nullptr;
	}

	cleanupCommandList();
	cleanupFence();

	cleanupOutputDiffuseBuffer();
	cleanupOutputDepthBuffer();

	cleanupShaderTables();


	// Cleanup DXRStateObject
	if (m_pDXRStateObject)
	{
		m_pDXRStateObject->Release();
		m_pDXRStateObject = nullptr;
	}
	// Cleanup RootSignature
	if (m_pRaytracingGlobalRootSignature)
	{
		m_pRaytracingGlobalRootSignature->Release();
		m_pRaytracingGlobalRootSignature = nullptr;
	}
	if (m_pRayShader)
	{
		pShaderManager->ReleaseShader(m_pRayShader);
		m_pRayShader = nullptr;
	}
	if (m_pBlasInstance)
	{
		FreeBLAS(m_pBlasInstance);
		m_pBlasInstance = nullptr;
	}
	if (m_pTLAS)
	{
		m_pTLAS->Release();
		m_pTLAS = nullptr;
	}
	if (m_pBLASInstanceDescResouce)
	{
		m_pBLASInstanceDescResouce->Release();
		m_pBLASInstanceDescResouce = nullptr;
	}
	cleanupMesh();

	cleanupDescriptorHeapForCBV_SRV_UAV();
	cleanupDispatchHeap();
}

void RayTracingManager::DoRaytracing(ID3D12GraphicsCommandList6* pCommandList)
{
	CD3DX12_CPU_DESCRIPTOR_HANDLE	dispatchHeapHandleCPU(m_pShaderVisibleDescriptorHeap->GetCPUDescriptorHandleForHeapStart());

	D3D12_CPU_DESCRIPTOR_HANDLE	cbvHandle = {};
	SimpleConstantBufferPool* pConstantBufferPool = m_pRenderer->GetConstantBufferPool(CONSTANT_BUFFER_TYPE_RAY_TRACING, 0);
	ConstantBufferContainer* pCB = pConstantBufferPool->Alloc();
	ASSERT(pCB, "Failed to allocate constant buffer.");

	CB_RayTracing* pConstBuffer = (CB_RayTracing*)pCB->pSystemMemAddr;
	pConstBuffer->MaxRadianceRayRecursionDepth = MAX_RADIANCE_RECURSION_DEPTH;
	pConstBuffer->Near = 0.1f;
	pConstBuffer->Far = 1000.0f;

	// (0) CBV - RayTracing
	m_pD3DDevice->CopyDescriptorsSimple(1, dispatchHeapHandleCPU, pCB->CBVHandle, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
	dispatchHeapHandleCPU.Offset(1, m_DescriptorSize);

	// (1) UAV - output diffuse
	CD3DX12_CPU_DESCRIPTOR_HANDLE uavDiffuse(m_pCommonDescriptorHeap->GetCPUDescriptorHandleForHeapStart(), COMMON_DESCRIPTOR_INDEX_OUTPUT_DIFFUSE_UAV, m_DescriptorSize);
	m_pD3DDevice->CopyDescriptorsSimple(1, dispatchHeapHandleCPU, uavDiffuse, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
	dispatchHeapHandleCPU.Offset(1, m_DescriptorSize);

	// (2) UAV - output depth
	CD3DX12_CPU_DESCRIPTOR_HANDLE uavDepth(m_pCommonDescriptorHeap->GetCPUDescriptorHandleForHeapStart(), COMMON_DESCRIPTOR_INDEX_OUTPUT_DEPTH_UAV, m_DescriptorSize);
	m_pD3DDevice->CopyDescriptorsSimple(1, dispatchHeapHandleCPU, uavDepth, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
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
}

BALSInstance* RayTracingManager::AllocBLAS(
	ID3D12Resource* pVertexBuffer,
	UINT vertexSize,
	UINT numVertices,
	const BLASBuilTriGroupInfo* pTriGroupInfoList,
	UINT numTriGroupInfos,
	bool bAllowUpdate)
{
	BALSInstance* pBlasInstance = buildBLAS(pVertexBuffer, vertexSize, numVertices, pTriGroupInfoList, numTriGroupInfos, bAllowUpdate);
	m_BlasInstanceList.emplace_back(pBlasInstance);

	return pBlasInstance;
}

void RayTracingManager::FreeBLAS(BALSInstance* pBlasHandle)
{
	BALSInstance* pBlasInstance = (BALSInstance*)pBlasHandle;

	m_BlasInstanceList.remove(pBlasInstance);

	if (pBlasInstance->pBLAS)
	{
		pBlasInstance->pBLAS->Release();
		pBlasInstance->pBLAS = nullptr;
	}
	free(pBlasInstance);
}

void RayTracingManager::UpdateWindowSize(UINT width, UINT height)
{
	cleanupOutputDiffuseBuffer();
	cleanupOutputDepthBuffer();

	m_Width = width;
	m_Height = height;
	createOutputDiffuseBuffer(m_Width, m_Height);
	createOutputDepthBuffer(m_Width, m_Height);
}

bool RayTracingManager::initAccelerationStructure()
{
	// TODO: 바깥에서 버텍스데이터와 텍스처를 입력하는 식으로 변경할 것
	ID3D12Device5* pD3DDeivce = m_pRenderer->GetD3DDevice();
	D3D12ResourceManager* pResourceManager = m_pRenderer->GetResourceManager();

	// Build BLAS
	BLASBuilTriGroupInfo buildInfo = {};

	buildInfo.pIB = m_pIndexBuffer;
	buildInfo.bNotOpaque = false;
	buildInfo.NumIndices = 6;

	m_pBlasInstance = AllocBLAS(m_pVertexBuffer, sizeof(SpriteVertex), 4, &buildInfo, 1, true);

	// Build Shader Tables
	buildShaderTables();

	// Build TLAS
	BALSInstance* ppBlasInstancelist[256] = {};
	UINT numBlasInstances = 0;

	for (auto pCurr : m_BlasInstanceList)
	{
		ASSERT(numBlasInstances < (uint64_t)_countof(ppBlasInstancelist), "Too many BLAS instances");
		ppBlasInstancelist[numBlasInstances] = pCurr;
		numBlasInstances++;
	}

	CreateUploadBuffer(m_pD3DDevice, nullptr, sizeof(D3D12_RAYTRACING_INSTANCE_DESC) * numBlasInstances, &m_pBLASInstanceDescResouce, L"InstanceDescs");

	m_pTLAS = buildTLAS(m_pBLASInstanceDescResouce, ppBlasInstancelist, numBlasInstances, false, 0);

	return true;
}

BALSInstance* RayTracingManager::buildBLAS(
	ID3D12Resource* pVertexBuffer,
	UINT vertexSize,
	UINT numVertices,
	const BLASBuilTriGroupInfo* pTriGroupInfoList,
	UINT numTriGroupInfos,
	bool bAllowUpdate)
{
	//
	// VB한개, IB여러개
	//
	// 추후에 ID3D12CommandList포인터와 UINT CurContextIndex를 받아서 중첩렌더링을 처리할 수 있도록 수정한다.
	//
	BALSInstance* pBlasInstance = nullptr;
	ID3D12Resource* pBLAS = nullptr;

	ID3D12Device5* pD3DDevice = m_pRenderer->GetD3DDevice();

	ASSERT(numTriGroupInfos < MAX_TRIGROUP_COUNT_PER_BLAS, "Too many triangle groups in BLAS");

	size_t blasInstanceMemSize = sizeof(BALSInstance) - sizeof(RootArgument) + sizeof(RootArgument) * numTriGroupInfos;
	pBlasInstance = (BALSInstance*)malloc(blasInstanceMemSize);
	memset(pBlasInstance, 0, blasInstanceMemSize);
	pBlasInstance->ID = 0;
	pBlasInstance->Transform = XMMatrixIdentity();
	pBlasInstance->NumVertices = numVertices;

	// Fill D3D12_RAYTRACING_GEOMETRY_DESC arrays


	D3D12_RAYTRACING_GEOMETRY_DESC pGeomDescList[MAX_TRIGROUP_COUNT_PER_BLAS] = {};
	D3D12_GPU_VIRTUAL_ADDRESS VB_GPU_Ptr = pVertexBuffer->GetGPUVirtualAddress();
	for (UINT i = 0; i < numTriGroupInfos; i++)
	{
		D3D12_GPU_VIRTUAL_ADDRESS IB_GPU_Ptr = pTriGroupInfoList[i].pIB->GetGPUVirtualAddress();

		pGeomDescList[i].Type = D3D12_RAYTRACING_GEOMETRY_TYPE_TRIANGLES;
		pGeomDescList[i].Triangles.IndexBuffer = IB_GPU_Ptr;
		pGeomDescList[i].Triangles.IndexCount = pTriGroupInfoList[i].NumIndices;
		pGeomDescList[i].Triangles.IndexFormat = DXGI_FORMAT_R16_UINT;
		pGeomDescList[i].Triangles.Transform3x4 = 0;
		pGeomDescList[i].Triangles.VertexFormat = DXGI_FORMAT_R32G32B32_FLOAT;
		pGeomDescList[i].Triangles.VertexCount = numVertices;
		pGeomDescList[i].Triangles.VertexBuffer.StartAddress = VB_GPU_Ptr;
		pGeomDescList[i].Triangles.VertexBuffer.StrideInBytes = vertexSize;
		// Mark the geometry as opaque. 
		// PERFORMANCE TIP: mark geometry as opaque whenever applicable as it can enable important ray processing optimizations.
		// Note: When rays encounter opaque geometry an any hit shader will not be executed whether it is present or not.
		if (pTriGroupInfoList[i].bNotOpaque)
		{
			pGeomDescList[i].Flags = D3D12_RAYTRACING_GEOMETRY_FLAG_NONE;
		}
		else
		{
			pGeomDescList[i].Flags = D3D12_RAYTRACING_GEOMETRY_FLAG_OPAQUE;
		}
	}

	// Build BLAS
	{
		// Get required sizes for an acceleration structure.
		D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS inputs = {};
		inputs.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY;
		if (bAllowUpdate)
		{
			inputs.Flags = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_BUILD | D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_ALLOW_UPDATE;
		}
		else
		{
			inputs.Flags = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_TRACE | D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_ALLOW_COMPACTION;
		}
		inputs.NumDescs = numTriGroupInfos;
		inputs.pGeometryDescs = pGeomDescList;
		inputs.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL;

		D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO info = {};
		pD3DDevice->GetRaytracingAccelerationStructurePrebuildInfo(&inputs, &info);

		ID3D12Resource* pScratchResource = nullptr;

		HRESULT hr = m_pD3DDevice->CreateCommittedResource(
			&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT),
			D3D12_HEAP_FLAG_NONE,
			&CD3DX12_RESOURCE_DESC::Buffer(info.ScratchDataSizeInBytes, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS),
			D3D12_RESOURCE_STATE_COMMON,
			nullptr, IID_PPV_ARGS(&pScratchResource));
		ASSERT(SUCCEEDED(hr), "Failed to create scratch resource for BLAS.");

		D3D12_GPU_VIRTUAL_ADDRESS pScratchGPUAddress = pScratchResource->GetGPUVirtualAddress();
		ASSERT(pScratchGPUAddress, "Invalid GPU address for scratch resource.");

		// Allocate resources for acceleration structures.
		// Acceleration structures can only be placed in resources that are created in the default heap (or custom heap equivalent). 
		// Default heap is OK since the application doesn't need CPU read/write access to them. 
		// The resources that will contain acceleration structures must be created in the state D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE, 
		// and must have resource flag D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS. The ALLOW_UNORDERED_ACCESS requirement simply acknowledges both: 
		//  - the system will be doing this type of access in its implementation of acceleration structure builds behind the scenes.
		//  - from the app point of view, synchronization of writes/reads to acceleration structures is accomplished using UAV barriers.
		D3D12_RESOURCE_STATES initialResourceState = D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE;
		hr = CreateUAVBuffer(m_pD3DDevice, info.ResultDataMaxSizeInBytes, &pBLAS, initialResourceState, L"BottomLevelAccelerationStructure");
		ASSERT(SUCCEEDED(hr), "Failed to create BLAS resource.");

		// Bottom Level Acceleration Structure desc
		D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC asDesc = {};
		asDesc.Inputs = inputs;
		asDesc.ScratchAccelerationStructureData = pScratchGPUAddress;
		asDesc.DestAccelerationStructureData = pBLAS->GetGPUVirtualAddress();

		hr = m_pCommandAllocator->Reset();
		ASSERT(SUCCEEDED(hr), "Failed to reset command allocator.");

		hr = m_pCommandList->Reset(m_pCommandAllocator, nullptr);
		ASSERT(SUCCEEDED(hr), "Failed to reset command list.");

		m_pCommandList->BuildRaytracingAccelerationStructure(&asDesc, 0, nullptr);

		// We need to insert a UAV barrier before using the acceleration structures in a raytracing operation
		m_pCommandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::UAV(pBLAS));

		m_pCommandList->Close();

		ID3D12CommandList* ppCommandLists[] = { m_pCommandList };
		m_pCommandQueue->ExecuteCommandLists(_countof(ppCommandLists), ppCommandLists);

		fence();
		waitForFenceValue();

		if (pScratchResource)
		{
			pScratchResource->Release();
			pScratchResource = nullptr;
		}
	}

	pBlasInstance->pBLAS = pBLAS;
	pBlasInstance->NumTriGroups = numTriGroupInfos;

	return pBlasInstance;
}

ID3D12Resource* RayTracingManager::buildTLAS(
	ID3D12Resource* pInstanceDescResource,
	BALSInstance** ppInstanceList,
	UINT numBlasInstances,
	bool bAllowUpdate,
	UINT currContextIndex)
{
	ID3D12Resource* pTLASResource = nullptr;

	// First, get the size of the TLAS buffers and create them
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
	inputs.NumDescs = numBlasInstances;
	inputs.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL;

	D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO info;
	m_pD3DDevice->GetRaytracingAccelerationStructurePrebuildInfo(&inputs, &info);

	ID3D12Resource* pScratchResource = nullptr;

	HRESULT hr = m_pD3DDevice->CreateCommittedResource(
		&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT),
		D3D12_HEAP_FLAG_NONE,
		&CD3DX12_RESOURCE_DESC::Buffer(info.ScratchDataSizeInBytes, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS),
		D3D12_RESOURCE_STATE_COMMON,
		nullptr, IID_PPV_ARGS(&pScratchResource));
	ASSERT(SUCCEEDED(hr), "Failed to create scratch resource for TLAS.");

	D3D12_GPU_VIRTUAL_ADDRESS pScratchGPUAddress = pScratchResource->GetGPUVirtualAddress();

	D3D12_RESOURCE_STATES initialResourceState = D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE;
	hr = CreateUAVBuffer(m_pD3DDevice, info.ResultDataMaxSizeInBytes, &pTLASResource, initialResourceState, L"TopLevelAccelerationStructure");
	ASSERT(SUCCEEDED(hr), "Failed to create TLAS resource.");

	// Allocate resources for acceleration structures.
	// Acceleration structures can only be placed in resources that are created in the default heap (or custom heap equivalent). 
	// Default heap is OK since the application doesn't need CPU read/write access to them. 
	// The resources that will contain acceleration structures must be created in the state D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE, 
	// and must have resource flag D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS. The ALLOW_UNORDERED_ACCESS requirement simply acknowledges both: 
	//  - the system will be doing this type of access in its implementation of acceleration structure builds behind the scenes.
	//  - from the app point of view, synchronization of writes/reads to acceleration structures is accomplished using UAV barriers.

	//
	// Create an instance desc for the bottom-level acceleration structure.
	//ID3D12Resource* pInstanceDescResource = nullptr;
	D3D12_RAYTRACING_INSTANCE_DESC* pInstanceDescList = nullptr;

	CD3DX12_RANGE readRange(0, 0);
	pInstanceDescResource->Map(0, &readRange, (void**)&pInstanceDescList);

	UINT numTlasElements = 0;
	D3D12_RAYTRACING_INSTANCE_DESC* pInstanceDescEntry = pInstanceDescList;
	for (UINT i = 0; i < numBlasInstances; i++)
	{
		const BALSInstance* pInstanceSrc = ppInstanceList[i];
		XMMATRIX matTranspose = XMMatrixTranspose(pInstanceSrc->Transform);
		memcpy(pInstanceDescEntry->Transform, &matTranspose, sizeof(pInstanceDescEntry->Transform));

		pInstanceDescEntry->InstanceID = pInstanceSrc->ID; // This value will be exposed to the shader via InstanceID()
		pInstanceDescEntry->InstanceContributionToHitGroupIndex = pInstanceSrc->ShaderRecordIndex;
		pInstanceDescEntry->Flags = D3D12_RAYTRACING_INSTANCE_FLAG_NONE;
		pInstanceDescEntry->AccelerationStructure = pInstanceSrc->pBLAS->GetGPUVirtualAddress();
		pInstanceDescEntry->InstanceMask = 0xFF;
		pInstanceDescEntry++;
		numTlasElements++;
	}

	// Unmap
	pInstanceDescResource->Unmap(0, nullptr);

	// Create the TLAS
	D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC asDesc = {};
	asDesc.Inputs = inputs;
	//asDesc.Inputs.NumDescs = dwBlasInstanceNum;
	asDesc.Inputs.NumDescs = numTlasElements;
	asDesc.Inputs.InstanceDescs = pInstanceDescResource->GetGPUVirtualAddress();
	asDesc.DestAccelerationStructureData = pTLASResource->GetGPUVirtualAddress();
	asDesc.ScratchAccelerationStructureData = pScratchGPUAddress;

	hr = m_pCommandAllocator->Reset();
	ASSERT(SUCCEEDED(hr), "Failed to reset command allocator.");

	hr = m_pCommandList->Reset(m_pCommandAllocator, nullptr);
	ASSERT(SUCCEEDED(hr), "Failed to reset command list.");

	m_pCommandList->BuildRaytracingAccelerationStructure(&asDesc, 0, nullptr);

	// We need to insert a UAV barrier before using the acceleration structures in a raytracing operation
	D3D12_RESOURCE_BARRIER uavBarrier = {};
	uavBarrier.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
	uavBarrier.UAV.pResource = pTLASResource;
	m_pCommandList->ResourceBarrier(1, &uavBarrier);

	m_pCommandList->Close();

	ID3D12CommandList* ppCommandLists[] = { m_pCommandList };
	m_pCommandQueue->ExecuteCommandLists(_countof(ppCommandLists), ppCommandLists);

	fence();
	waitForFenceValue();

	if (pScratchResource)
	{
		pScratchResource->Release();
		pScratchResource = nullptr;
	}

	return pTLASResource;
}

bool RayTracingManager::createOutputDiffuseBuffer(UINT width, UINT height)
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
	CD3DX12_CPU_DESCRIPTOR_HANDLE	uavHandle(m_pCommonDescriptorHeap->GetCPUDescriptorHandleForHeapStart(), COMMON_DESCRIPTOR_INDEX_OUTPUT_DIFFUSE_UAV, m_DescriptorSize);
	m_pD3DDevice->CreateUnorderedAccessView(m_pOutputDiffuse, nullptr, nullptr, uavHandle);

	return true;
}

void RayTracingManager::cleanupOutputDiffuseBuffer()
{
	if (m_pOutputDiffuse)
	{
		m_pOutputDiffuse->Release();
		m_pOutputDiffuse = nullptr;
	}
}

bool RayTracingManager::createOutputDepthBuffer(UINT width, UINT height)
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

	CD3DX12_CPU_DESCRIPTOR_HANDLE	uavHandle(m_pCommonDescriptorHeap->GetCPUDescriptorHandleForHeapStart(), COMMON_DESCRIPTOR_INDEX_OUTPUT_DEPTH_UAV, m_DescriptorSize);
	m_pD3DDevice->CreateUnorderedAccessView(m_pOutputDepth, nullptr, &uavDesc, uavHandle);

	return true;
}

void RayTracingManager::cleanupOutputDepthBuffer()
{
	if (m_pOutputDepth)
	{
		m_pOutputDepth->Release();
		m_pOutputDepth = nullptr;
	}
}

void RayTracingManager::createRootSignatures()
{
	// Global Root Signature
	// This is a root signature that is shared across all raytracing shaders invoked during a DispatchRays() call.

	// root param 0
	// output-diffuse(uav) | output-depth(uav)

	// root param 1
	// Acceleration Sturecture

	CD3DX12_DESCRIPTOR_RANGE globalRanges[2] = {};
	globalRanges[0].Init(D3D12_DESCRIPTOR_RANGE_TYPE_CBV, 1, 0);	// b0 : CBV

	// u0 : u0-diffuse | u1 : out-depth
	globalRanges[1].Init(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 2, 0);

	// b0 : RaytracingCBV | u0 : u0-diffuse | u1 : out-depth | t0 : AccelerationStructure
	CD3DX12_ROOT_PARAMETER GlobalRootParameters[2] = {};
	GlobalRootParameters[0].InitAsDescriptorTable(_countof(globalRanges), globalRanges, D3D12_SHADER_VISIBILITY_ALL);
	GlobalRootParameters[1].InitAsShaderResourceView(0);	// Acceleration Structure


	// 샘플러
	D3D12_STATIC_SAMPLER_DESC samplers[4] = {};
	SetSamplerDesc_Wrap(samplers + 0, 0);	// Wrap Linear
	SetSamplerDesc_Clamp(samplers + 1, 1);	// Clamp Linear
	SetSamplerDesc_Wrap(samplers + 2, 2);	// Wrap Point
	samplers[2].Filter = D3D12_FILTER_MIN_MAG_MIP_POINT;
	SetSamplerDesc_Mirror(samplers + 3, 3);	// Mirror Linear
	samplers[3].Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;

	for (UINT i = 0; i < (UINT)_countof(samplers); i++)
	{
		samplers[i].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
	}

	CD3DX12_ROOT_SIGNATURE_DESC globalRootSignatureDesc(ARRAYSIZE(GlobalRootParameters), GlobalRootParameters, _countof(samplers), samplers);
	SerializeAndCreateRaytracingRootSignature(m_pD3DDevice, &globalRootSignatureDesc, &m_pRaytracingGlobalRootSignature);
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

	//
	// DXIL 라이브러리에서 사용할 쉐이더 export들을 정의
	//
	pLib->DefineExport(g_RaygenShaderName);

	// HitGroup에서 import할 수 있도록 export
	// 쉐이더 타입별(radiance/shadow)로 Closest Hit, Any Hit, Miss 쉐이더를 export


	pLib->DefineExport(g_ClosestHitShaderName[0]);	// hit group에서 import할 수 있도록 export
	pLib->DefineExport(g_AnyHitShaderName[0]);
	pLib->DefineExport(g_MissShaderName[0]);

	// 2) Triangle hit group
	// 히트 그룹 Subobject 생성
	// 히트 그룹은 Geometry에 레이가 교차했을 때 실행할 ClosestHit, AnyHit, Intersection 쉐이더를 정의

	CD3DX12_HIT_GROUP_SUBOBJECT* pHitGroup = raytracingPipeline.CreateSubobject<CD3DX12_HIT_GROUP_SUBOBJECT>();
	pHitGroup->SetClosestHitShaderImport(g_ClosestHitShaderName[0]);
	pHitGroup->SetAnyHitShaderImport(g_AnyHitShaderName[0]);
	pHitGroup->SetHitGroupExport(g_HitGroupName[0]);
	pHitGroup->SetHitGroupType(D3D12_HIT_GROUP_TYPE_TRIANGLES);
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
	// 이 샘플에서는 필요하지 않다. 생략한다.
	//
	//


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
	m_pMissShaderTable->CommitResource(1);

	void* pMissShaderIdentifier = pStateObjectProperties->GetShaderIdentifier(g_MissShaderName[0]);
	ShaderRecord missShaderRecord = ShaderRecord(pMissShaderIdentifier, m_ShaderIdentifierSize);
	m_pMissShaderTable->InsertShaderRecord(&missShaderRecord);
	m_MissShaderTableStrideInBytes = m_pMissShaderTable->GetShaderRecordSize();

	// hitgroup Shader Table
	m_pHitGroupShaderTable = new ShaderTable;
	m_pHitGroupShaderTable->Initiailze(m_pD3DDevice, m_ShaderIdentifierSize + sizeof(RootArgument), L"HitGroupShaderTable");
	void* pHitGroupShaderIdentifier = pStateObjectProperties->GetShaderIdentifier(g_HitGroupName[0]);

	RootArgument rootArg = {};	// 이 샘플에서는 hit시에 전달할 파라미터(texture, vertex data등)가 없음.
	m_pHitGroupShaderTable->CommitResource(1);
	ShaderRecord record = ShaderRecord(pHitGroupShaderIdentifier, m_ShaderIdentifierSize, &rootArg, sizeof(RootArgument));
	m_pHitGroupShaderTable->InsertShaderRecord(&record);
	m_HitGroupShaderTableStrideInBytes = m_pHitGroupShaderTable->GetShaderRecordSize();
	m_HitGroupShaderRecordNum = m_pHitGroupShaderTable->GetShaderRecordNum();

	if (pStateObjectProperties)
	{
		pStateObjectProperties->Release();
		pStateObjectProperties = nullptr;
	}
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

void RayTracingManager::cleanupDescriptorHeapForCBV_SRV_UAV()
{
	if (m_pCommonDescriptorHeap)
	{
		m_pCommonDescriptorHeap->Release();
		m_pCommonDescriptorHeap = nullptr;
	}
}

void RayTracingManager::createShaderVisibleHeap()
{
	D3D12_DESCRIPTOR_HEAP_DESC HeapDesc = {};
	HeapDesc.NumDescriptors = DISPATCH_DESCRIPTOR_INDEX_COUNT;
	HeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
	HeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;

	HRESULT hr = m_pD3DDevice->CreateDescriptorHeap(&HeapDesc, IID_PPV_ARGS(&m_pShaderVisibleDescriptorHeap));
	ASSERT(SUCCEEDED(hr), "Failed to create shader visible descriptor heap.");
}

void RayTracingManager::cleanupDispatchHeap()
{
	if (m_pShaderVisibleDescriptorHeap)
	{
		m_pShaderVisibleDescriptorHeap->Release();
		m_pShaderVisibleDescriptorHeap = nullptr;
	}
}

void RayTracingManager::createCommandList()
{
	HRESULT hr = S_OK;
	hr = m_pD3DDevice->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&m_pCommandAllocator));
	ASSERT(SUCCEEDED(hr), "Failed to create command allocator");
	// Create the command list.
	hr = m_pD3DDevice->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, m_pCommandAllocator, nullptr, IID_PPV_ARGS(&m_pCommandList));
	ASSERT(SUCCEEDED(hr), "Failed to create command list");
	// Command lists are created in the recording state, but there is nothing
	// to record yet. The main loop expects it to be closed, so close it now.
	m_pCommandList->Close();
}

void RayTracingManager::cleanupCommandList()
{
	if (m_pCommandList)
	{
		m_pCommandList->Release();
		m_pCommandList = nullptr;
	}
	if (m_pCommandAllocator)
	{
		m_pCommandAllocator->Release();
		m_pCommandAllocator = nullptr;
	}
}

void RayTracingManager::createFence()
{
	HRESULT hr = S_OK;
	// Create synchronization objects and wait until assets have been uploaded to the GPU.
	hr = m_pD3DDevice->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&m_pFence));
	ASSERT(SUCCEEDED(hr), "Failed to create fence");

	m_ui64FenceValue = 0;

	// Create an event handle to use for frame synchronization.
	m_hFenceEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);
}

void RayTracingManager::cleanupFence()
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

uint64_t RayTracingManager::fence()
{
	m_ui64FenceValue++;
	m_pCommandQueue->Signal(m_pFence, m_ui64FenceValue);
	return m_ui64FenceValue;
}

void RayTracingManager::waitForFenceValue()
{
	const uint64_t ExpectedFenceValue = m_ui64FenceValue;

	// Wait until the previous frame is finished.
	if (m_pFence->GetCompletedValue() < ExpectedFenceValue)
	{
		m_pFence->SetEventOnCompletion(ExpectedFenceValue, m_hFenceEvent);
		WaitForSingleObject(m_hFenceEvent, INFINITE);
	}
}

bool RayTracingManager::initMesh()
{
	bool bResult = false;
	HRESULT hr = S_OK;

	ID3D12Device5* pD3DDeivce = m_pRenderer->GetD3DDevice();
	D3D12ResourceManager* pResourceManager = m_pRenderer->GetResourceManager();

	// TODO: 바깥에서 버텍스데이터와 텍스처를 입력하는 식으로 변경할 것
	// Create the vertex buffer.
	SpriteVertex vertices[] =
	{
		{ { -0.25f, 0.25f, 0.1f }, { 0.0f, 0.0f, 1.0f, 1.0f }, { 0.0f, 1.0f }},
		{ { 0.25f, 0.25f, 0.1f }, { 1.0f, 0.0f, 0.0f, 1.0f }, { 0.0f, 0.0f }},
		{ { 0.25f, -0.25f, 0.1f }, { 0.0f, 1.0f, 0.0f, 1.0f }, { 1.0f, 0.0f } },
		{ { -0.25f, -0.25f, 0.1f }, { 0.0f, 0.5f, 0.5f, 1.0f }, { 1.0f, 1.0f } }
	};

	uint16_t indices[] =
	{
		0, 1, 2,
		0, 2, 3
	};

	D3D12_VERTEX_BUFFER_VIEW vertexBufferView = {};
	D3D12_INDEX_BUFFER_VIEW indexBufferView = {};

	if (FAILED(pResourceManager->CreateVertexBuffer(sizeof(SpriteVertex), (size_t)_countof(vertices), &vertexBufferView, &m_pVertexBuffer, vertices, false)))
	{
		ASSERT(false, "Failed to create vertex buffer");
		goto lb_return;
	}

	const size_t numFaces = 2;
	size_t indicesSize = numFaces * 3 * sizeof(uint16_t);
	size_t alignedIndicesSize = (indicesSize / 16 + ((indicesSize % 16) != 0)) * 16;
	size_t alignedIndexNum = alignedIndicesSize / sizeof(uint16_t);

	if (FAILED(pResourceManager->CreateIndexBuffer(alignedIndexNum, &indexBufferView, &m_pIndexBuffer, indices, sizeof(indices) > 0)))
	{
		ASSERT(false, "Failed to create index buffer");
		goto lb_return;
	}

	bResult = true;
lb_return:
	return bResult;
}

void RayTracingManager::cleanupMesh()
{
	if (m_pVertexBuffer)
	{
		m_pVertexBuffer->Release();
		m_pVertexBuffer = nullptr;
	}
	if (m_pIndexBuffer)
	{
		m_pIndexBuffer->Release();
		m_pIndexBuffer = nullptr;
	}
}