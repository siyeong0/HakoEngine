#include "pch.h"
#include "D3D12Renderer.h"
#include "CommandListPool.h"
#include "CasperObject.h"
#include "BasicMeshObject.h"
#include "ProceduralSphereObject.h"
#include "SpriteObject.h"
#include "RootSignatureManager.h"
#include "SimpleConstantBufferPool.h"
#include "DescriptorPool.h"
#include "RenderQueueRayTracing.h"

bool RenderQueueRayTracing::Initialize(D3D12Renderer* pRenderer, int MaxNumItems)
{
	m_pRenderer = pRenderer;
	m_MaxBufferSize = sizeof(RenderItem) * MaxNumItems;
	m_pBuffer = (char*)malloc(m_MaxBufferSize);
	memset(m_pBuffer, 0, m_MaxBufferSize);

	return true;
}

bool RenderQueueRayTracing::Add(const RenderItem* pItem)
{
	ASSERT(m_AllocatedSize + sizeof(RenderItem) <= m_MaxBufferSize);

	char* pDest = m_pBuffer + m_AllocatedSize;
	memcpy(pDest, pItem, sizeof(RenderItem));
	m_AllocatedSize += sizeof(RenderItem);
	m_ItemCount++;

	return true;
}

int RenderQueueRayTracing::Process(
	int threadIndex,
	CommandListPool* pCommandListPool,
	ID3D12CommandQueue* pCommandQueue,
	int numProcessPerCmdList,
	D3D12_CPU_DESCRIPTOR_HANDLE rtv,
	D3D12_CPU_DESCRIPTOR_HANDLE dsv,
	const D3D12_VIEWPORT* pViewport,
	const D3D12_RECT* pScissorRect)
{
	// Command list for remaining commands.
	ID3D12GraphicsCommandList6* ppCommandList[64] = {};
	uint numCmdLists = 0;

	ID3D12GraphicsCommandList6* pCurrCommandList = nullptr;
	int processCount = 0;
	int processCountPerCmdList = 0;
	const RenderItem* pItem = nullptr;
	while (pItem = dispatch())
	{
		pCurrCommandList = pCommandListPool->GetCurrentCommandList();
		switch (pItem->Type)
		{
		case RENDER_ITEM_TYPE_MESH_OBJ:
		{
			BasicMeshObject* meshObj = reinterpret_cast<BasicMeshObject*>(pItem->pObjHandle);
			meshObj->UpdateBLAS(threadIndex, pCurrCommandList, pItem->MeshObjParam.WorldMatrix);
		}
		break;
		case RENDER_ITEM_TYPE_PROCEDURAL_SPHERE_OBJ:
		{
			ProceduralSphereObject* proceduralObj = reinterpret_cast<ProceduralSphereObject*>(pItem->pObjHandle);
			proceduralObj->UpdateBLAS(threadIndex, pCurrCommandList, pItem->MeshObjParam.WorldMatrix);
		}
		break;
		case RENDER_ITEM_TYPE_CASPER_OBJ:
		{
			CasperObject* casperObj = reinterpret_cast<CasperObject*>(pItem->pObjHandle);
			casperObj->UpdateBLAS(threadIndex, pCurrCommandList, pItem->MeshObjParam.WorldMatrix);
		}
		break;
		case RENDER_ITEM_TYPE_SPRITE:
		{
			// Sprites are not supported in ray tracing queue.
			// Use rasterization queue instead.
		}
		break;
		default:
			ASSERT(false, "Unknown RenderItem type");
		}

		processCount++;
		processCountPerCmdList++;
		if (processCountPerCmdList > numProcessPerCmdList)
		{
			//pCommandListPool->CloseAndExecute(pCommandQueue);
			pCommandListPool->Close();
			ppCommandList[numCmdLists] = pCurrCommandList;
			numCmdLists++;
			pCurrCommandList = nullptr;
			processCountPerCmdList = 0;
		}
	}

	// Process remaining commands.
	if (processCountPerCmdList)
	{
		//pCommandListPool->CloseAndExecute(pCommandQueue);
		pCommandListPool->Close();
		ppCommandList[numCmdLists] = pCurrCommandList;
		numCmdLists++;
		pCurrCommandList = nullptr;
		processCountPerCmdList = 0;
	}

	if (numCmdLists)
	{
		pCommandQueue->ExecuteCommandLists(numCmdLists, (ID3D12CommandList**)ppCommandList);
	}

	m_ItemCount = 0;

	return processCount;
}

void RenderQueueRayTracing::Reset()
{
	m_AllocatedSize = 0;
	m_ReadBufferPos = 0;
}

void RenderQueueRayTracing::Cleanup()
{
	if (m_pBuffer)
	{
		free(m_pBuffer);
		m_pBuffer = nullptr;
	}
}

const RenderItem* RenderQueueRayTracing::dispatch()
{
	if (m_ReadBufferPos + sizeof(RenderItem) > m_AllocatedSize)
	{
		return nullptr;
	}

	const RenderItem* pItem = reinterpret_cast<const RenderItem*>(m_pBuffer + m_ReadBufferPos);
	m_ReadBufferPos += sizeof(RenderItem);

	return pItem;
}
