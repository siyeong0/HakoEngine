#include "pch.h"
#include "D3D12Renderer.h"
#include "CommandListPool.h"
#include "BasicMeshObject.h"
#include "SpriteObject.h"
#include "RootSignatureManager.h"
#include "SimpleConstantBufferPool.h"
#include "DescriptorPool.h"
#include "RenderQueue.h"

bool RenderQueue::Initialize(D3D12Renderer* pRenderer, int MaxNumItems)
{
	m_pRenderer = pRenderer;
	m_MaxBufferSize = sizeof(RenderItem) * MaxNumItems;
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
	int threadIndex, 
	CommandListPool* pCommandListPool, 
	ID3D12CommandQueue* pCommandQueue, 
	int numProcessPerCmdList,
	D3D12_CPU_DESCRIPTOR_HANDLE rtv, 
	D3D12_CPU_DESCRIPTOR_HANDLE dsv, 
	const D3D12_VIEWPORT* pViewport, 
	const D3D12_RECT* pScissorRect)
{
	ID3D12Device5* pD3DDevice = m_pRenderer->GetD3DDevice();
	RootSignatureManager* pRootSignatureManage = m_pRenderer->GetRootSignatureManager();

	// Descriptor heap for SRV.
	DescriptorPool* pDescriptorPool = m_pRenderer->GetDescriptorPool(threadIndex);
	ID3D12DescriptorHeap* pSRVDescriptorHeap = pDescriptorPool->GetDescriptorHeap();

	// Prepare per-frame constant buffer.
	SimpleConstantBufferPool* pConstantBufferPool = m_pRenderer->GetConstantBufferPool(CONSTANT_BUFFER_TYPE_PER_FRAME, threadIndex);
	ConstantBufferContainer* pCB = pConstantBufferPool->Alloc();
	ASSERT(pCB, "Failed to allocate constant buffer.");

	CONSTANT_BUFFER_PER_FRAME* pCBPerFrame = (CONSTANT_BUFFER_PER_FRAME*)pCB->pSystemMemAddr;
	const CONSTANT_BUFFER_PER_FRAME& srcCBData = m_pRenderer->GetFrameCBData();
	std::memcpy(pCBPerFrame, &srcCBData, sizeof(CONSTANT_BUFFER_PER_FRAME));

	// Command list for remaining commands.
	ID3D12GraphicsCommandList6* ppCommandList[64] = {};
	uint numCmdLists = 0;

	ID3D12GraphicsCommandList6* pCurrCommandList = nullptr;
	int processCount = 0;
	int processCountPerCmdList = 0;
	const RenderItem* pItem = nullptr;
	while (pItem = dispatch())
	{
		ID3D12GraphicsCommandList6* pNextCommandList = pCommandListPool->GetCurrentCommandList();
		if (pNextCommandList != pCurrCommandList)
		{
			pCurrCommandList = pNextCommandList;

			pCurrCommandList->RSSetViewports(1, pViewport);
			pCurrCommandList->RSSetScissorRects(1, pScissorRect);
			pCurrCommandList->OMSetRenderTargets(1, &rtv, FALSE, &dsv);

			pCurrCommandList->SetGraphicsRootSignature(pRootSignatureManage->Query(ERootSignatureType::GraphicsDefault));
			pCurrCommandList->SetDescriptorHeaps(1, &pSRVDescriptorHeap);

			pCurrCommandList->SetGraphicsRootConstantBufferView(ROOT_SLOT_CBV_PER_FRAME, pCB->pGPUMemAddr);
		}

		switch (pItem->Type)
		{
			case RENDER_ITEM_TYPE_MESH_OBJ:
				{
					BasicMeshObject* meshObj = reinterpret_cast<BasicMeshObject*>(pItem->pObjHandle);
					meshObj->Draw(threadIndex, pCurrCommandList, &pItem->MeshObjParam.matWorld);
				}
				break;
			case RENDER_ITEM_TYPE_SPRITE:
				{
					SpriteObject* spriteObj = reinterpret_cast<SpriteObject*>(pItem->pObjHandle);
					TextureHandle* texureHandle = reinterpret_cast<TextureHandle*>(pItem->SpriteParam.pTexHandle);
					float z = pItem->SpriteParam.Z;

					if (texureHandle)
					{
						XMFLOAT2 position = { (float)pItem->SpriteParam.PosX, (float)pItem->SpriteParam.PosY };
						XMFLOAT2 scale = { pItem->SpriteParam.ScaleX, pItem->SpriteParam.ScaleY };
						
						const RECT* pRect = nullptr;
						if (pItem->SpriteParam.bUseRect)
						{
							pRect = &pItem->SpriteParam.Rect;
						}

						if (texureHandle->pUploadBuffer)
						{
							if (texureHandle->bUpdated)
							{
								D3DUtil::UpdateTexture(pD3DDevice, pCurrCommandList, texureHandle->pTexResource, texureHandle->pUploadBuffer);
							}
							texureHandle->bUpdated = false;
						}
						spriteObj->DrawWithTex(threadIndex, pCurrCommandList, &position, &scale, pRect, z, texureHandle);
					}
					else
					{
						SpriteObject* spriteObj = reinterpret_cast<SpriteObject*>(pItem->pObjHandle);
						XMFLOAT2 position = { (float)pItem->SpriteParam.PosX, (float)pItem->SpriteParam.PosY };
						XMFLOAT2 scale = { pItem->SpriteParam.ScaleX, pItem->SpriteParam.ScaleY };

						spriteObj->Draw(threadIndex, pCurrCommandList, &position, &scale, z);
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
