#include "pch.h"

#include "D3D12Renderer.h"
#include "CommandListPool.h"
#include "BasicMeshObject.h"
#include "SpriteObject.h"

#include "RenderQueue.h"

bool RenderQueue::Initialize(D3D12Renderer* pRenderer, int dwMaxItemNum)
{
	m_pRenderer = pRenderer;
	m_MaxBufferSize = sizeof(RenderItem) * dwMaxItemNum;
	m_pBuffer = (char*)malloc(m_MaxBufferSize);
	memset(m_pBuffer, 0, m_MaxBufferSize);

	return true;
}

bool RenderQueue::Add(const RenderItem* pItem)
{
	ASSERT(m_AllocatedSize + sizeof(RenderItem) <= m_MaxBufferSize);

	char* pDest = m_pBuffer + m_AllocatedSize;
	memcpy(pDest, pItem, sizeof(RenderItem));
	m_AllocatedSize += sizeof(RenderItem);
	m_ItemCount++;
	
	return true;
}

int RenderQueue::Process(
	int threadIndex, CommandListPool* pCommandListPool, ID3D12CommandQueue* pCommandQueue, int numProcessPerCmdList,
	D3D12_CPU_DESCRIPTOR_HANDLE rtv, D3D12_CPU_DESCRIPTOR_HANDLE dsv, const D3D12_VIEWPORT* pViewport, const D3D12_RECT* pScissorRect)
{
	ID3D12Device5* pD3DDevice = m_pRenderer->GetD3DDevice();
	
	ID3D12GraphicsCommandList6* ppCommandList[64] = {};
	UINT numCmdLists = 0;

	ID3D12GraphicsCommandList6* pCommandList = nullptr;
	int processCount = 0;
	int processCountPerCmdList = 0;
	const RenderItem* pItem = nullptr;
	while (pItem = dispatch())
	{
		pCommandList = pCommandListPool->GetCurrentCommandList();
		pCommandList->RSSetViewports(1, pViewport);
		pCommandList->RSSetScissorRects(1, pScissorRect);
		pCommandList->OMSetRenderTargets(1, &rtv, FALSE, &dsv);

		switch (pItem->Type)
		{
			case RENDER_ITEM_TYPE_MESH_OBJ:
				{
					BasicMeshObject* meshObj = reinterpret_cast<BasicMeshObject*>(pItem->pObjHandle);
					meshObj->Draw(threadIndex, pCommandList, &pItem->MeshObjParam.matWorld);
				}
				break;
			case RENDER_ITEM_TYPE_SPRITE:
				{
					SpriteObject* spriteObj = reinterpret_cast<SpriteObject*>(pItem->pObjHandle);
					TextureHandle* texureHandle = reinterpret_cast<TextureHandle*>(pItem->SpriteParam.pTexHandle);
					float Z = pItem->SpriteParam.Z;

					if (texureHandle)
					{
						XMFLOAT2 position = { (float)pItem->SpriteParam.iPosX, (float)pItem->SpriteParam.iPosY };
						XMFLOAT2 scale = { pItem->SpriteParam.fScaleX, pItem->SpriteParam.fScaleY };
						
						const RECT* pRect = nullptr;
						if (pItem->SpriteParam.bUseRect)
						{
							pRect = &pItem->SpriteParam.Rect;
						}

						if (texureHandle->pUploadBuffer)
						{
							if (texureHandle->bUpdated)
							{
								UpdateTexture(pD3DDevice, pCommandList, texureHandle->pTexResource, texureHandle->pUploadBuffer);
							}
							texureHandle->bUpdated = false;
						}
						spriteObj->DrawWithTex(threadIndex, pCommandList, &position, &scale, pRect, Z, texureHandle);
					}
					else
					{
						SpriteObject* spriteObj = reinterpret_cast<SpriteObject*>(pItem->pObjHandle);
						XMFLOAT2 position = { (float)pItem->SpriteParam.iPosX, (float)pItem->SpriteParam.iPosY };
						XMFLOAT2 scale = { pItem->SpriteParam.fScaleX, pItem->SpriteParam.fScaleY };

						spriteObj->Draw(threadIndex, pCommandList, &position, &scale, Z);
					}
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
			ppCommandList[numCmdLists] = pCommandList;
			numCmdLists++;
			pCommandList = nullptr;
			processCountPerCmdList = 0;
		}
	}
	
	// Process remaining commands.
	if (processCountPerCmdList)
	{
		//pCommandListPool->CloseAndExecute(pCommandQueue);
		pCommandListPool->Close();
		ppCommandList[numCmdLists] = pCommandList;
		numCmdLists++;
		pCommandList = nullptr;
		processCountPerCmdList = 0;
	}

	if (numCmdLists)
	{
		pCommandQueue->ExecuteCommandLists(numCmdLists, (ID3D12CommandList**)ppCommandList);
	}

	m_ItemCount = 0;

	return processCount;
}

void RenderQueue::Reset()
{
	m_AllocatedSize = 0;
	m_ReadBufferPos = 0;
}

void RenderQueue::Cleanup()
{
	if (m_pBuffer)
	{
		free(m_pBuffer);
		m_pBuffer = nullptr;
	}
}

const RenderItem* RenderQueue::dispatch()
{
	if (m_ReadBufferPos + sizeof(RenderItem) > m_AllocatedSize)
	{
		return nullptr;
	}

	const RenderItem* pItem = reinterpret_cast<const RenderItem*>(m_pBuffer + m_ReadBufferPos);
	m_ReadBufferPos += sizeof(RenderItem);

	return pItem;

}
