#include "pch.h"
#include "D3D12Renderer.h"
#include "CommandListPool.h"
#include "BasicMeshObject.h"
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
	int processCount = 0;
	const RenderItem* pItem = nullptr;
	while (pItem = dispatch())
	{
		switch (pItem->Type)
		{
		case RENDER_ITEM_TYPE_MESH_OBJ:
		{
			BasicMeshObject* meshObj = reinterpret_cast<BasicMeshObject*>(pItem->pObjHandle);
			meshObj->UpdateBLASTransform(pItem->MeshObjParam.matWorld);
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
