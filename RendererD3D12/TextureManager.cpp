#include "pch.h"

#include "SingleDescriptorAllocator.h"
#include "D3D12Renderer.h"
#include "D3D12ResourceManager.h"

#include "TextureManager.h"

bool TextureManager::Initialize(D3D12Renderer* pRenderer, UINT maxNumBuckets, UINT maxNumFiles)
{
	m_pRenderer = pRenderer;
	m_pResourceManager = pRenderer->GetResourceManager();

	m_pHashTable = new CHashTable;
	m_pHashTable->Initialize(maxNumBuckets, _MAX_PATH, maxNumFiles);

	return true;
}

TextureHandle* TextureManager::CreateTextureFromFile(const WCHAR* wchFileName)
{
	ID3D12Device* pD3DDevice = m_pRenderer->GetD3DDevice();
	SingleDescriptorAllocator* pSingleDescriptorAllocator = m_pRenderer->GetSingleDescriptorAllocator();
	bool bUseGpuUploadHeaps = m_pRenderer->IsGpuUploadHeapsEnabledInl();

	ID3D12Resource* pTexResource = nullptr;
	D3D12_CPU_DESCRIPTOR_HANDLE srv = {};
	D3D12_RESOURCE_DESC	desc = {};
	TextureHandle* pTexHandle = nullptr;

	size_t fileNameLen = wcslen(wchFileName);
	size_t keySize = fileNameLen * sizeof(WCHAR);
	if (m_pHashTable->Select((void**)&pTexHandle, 1, wchFileName, keySize))
	{
		pTexHandle->RefCount++;
	}
	else
	{
		if (m_pResourceManager->CreateTextureFromFile(&pTexResource, &desc, wchFileName, bUseGpuUploadHeaps))
		{
			D3D12_SHADER_RESOURCE_VIEW_DESC SRVDesc = {};
			SRVDesc.Format = desc.Format;
			SRVDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
			SRVDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
			SRVDesc.Texture2D.MipLevels = desc.MipLevels;

			if (pSingleDescriptorAllocator->AllocDescriptorHandle(&srv))
			{
				pD3DDevice->CreateShaderResourceView(pTexResource, &SRVDesc, srv);

				pTexHandle = allocTextureHandle();
				pTexHandle->pTexResource = pTexResource;
				pTexHandle->bFromFile = TRUE;
				pTexHandle->SRV = srv;

				pTexHandle->pSearchHandle = m_pHashTable->Insert((void*)pTexHandle, wchFileName, keySize);
				ASSERT(pTexHandle->pSearchHandle, "HashTable insertion failed.\n");
			}
			else
			{
				pTexResource->Release();
				pTexResource = nullptr;
			}
		}
	}
	return pTexHandle;
}

TextureHandle* TextureManager::CreateDynamicTexture(UINT texWidth, UINT texHeight)
{
	ID3D12Device* pD3DDevice = m_pRenderer->GetD3DDevice();
	SingleDescriptorAllocator* pSingleDescriptorAllocator = m_pRenderer->GetSingleDescriptorAllocator();
	TextureHandle* pTexHandle = nullptr;

	ID3D12Resource* pTexResource = nullptr;
	ID3D12Resource* pUploadBuffer = nullptr;
	D3D12_CPU_DESCRIPTOR_HANDLE srv = {};

	DXGI_FORMAT texFormat = DXGI_FORMAT_R8G8B8A8_UNORM;

	if (m_pResourceManager->CreateTexturePair(&pTexResource, &pUploadBuffer, texWidth, texHeight, texFormat))
	{
		D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
		srvDesc.Format = texFormat;
		srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
		srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
		srvDesc.Texture2D.MipLevels = 1;

		if (pSingleDescriptorAllocator->AllocDescriptorHandle(&srv))
		{
			pD3DDevice->CreateShaderResourceView(pTexResource, &srvDesc, srv);

			pTexHandle = allocTextureHandle();
			pTexHandle->pTexResource = pTexResource;
			pTexHandle->pUploadBuffer = pUploadBuffer;
			pTexHandle->SRV = srv;
		}
		else
		{
			pTexResource->Release();
			pTexResource = nullptr;

			pUploadBuffer->Release();
			pUploadBuffer = nullptr;
		}
	}

	return pTexHandle;
}

TextureHandle* TextureManager::CreateImmutableTexture(UINT texWidth, UINT texHeight, DXGI_FORMAT format, const uint8_t* pInitImage)
{
	ID3D12Device* pD3DDevice = m_pRenderer->GetD3DDevice();
	SingleDescriptorAllocator* pSingleDescriptorAllocator = m_pRenderer->GetSingleDescriptorAllocator();
	TextureHandle* pTexHandle = nullptr;

	ID3D12Resource* pTexResource = nullptr;
	D3D12_CPU_DESCRIPTOR_HANDLE srv = {};

	if (m_pResourceManager->CreateTexture(&pTexResource, texWidth, texHeight, format, pInitImage))
	{
		D3D12_SHADER_RESOURCE_VIEW_DESC SRVDesc = {};
		SRVDesc.Format = format;
		SRVDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
		SRVDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
		SRVDesc.Texture2D.MipLevels = 1;

		if (pSingleDescriptorAllocator->AllocDescriptorHandle(&srv))
		{
			pD3DDevice->CreateShaderResourceView(pTexResource, &SRVDesc, srv);

			pTexHandle = allocTextureHandle();
			pTexHandle->pTexResource = pTexResource;
			pTexHandle->SRV = srv;
		}
		else
		{
			pTexResource->Release();
			pTexResource = nullptr;
		}
	}

	return pTexHandle;
}

void TextureManager::DeleteTexture(TextureHandle* pTexHandle)
{
	freeTextureHandle(pTexHandle);
}

void TextureManager::Cleanup()
{
	ASSERT(!m_pTexLinkHead, "Texture resource leak detected.\n");
	if (m_pHashTable)
	{
		delete m_pHashTable;
		m_pHashTable = nullptr;
	}
}

TextureHandle* TextureManager::allocTextureHandle()
{
	TextureHandle* pTexHandle = new TextureHandle;
	memset(pTexHandle, 0, sizeof(TextureHandle));
	pTexHandle->Link.pItem = pTexHandle;
	LinkToLinkedListFIFO(&m_pTexLinkHead, &m_pTexLinkTail, &pTexHandle->Link);
	pTexHandle->RefCount = 1;
	return pTexHandle;
}

UINT TextureManager::freeTextureHandle(TextureHandle* pTexHandle)
{
	ID3D12Device* pD3DDevice = m_pRenderer->GetD3DDevice();
	SingleDescriptorAllocator* pSingleDescriptorAllocator = m_pRenderer->GetSingleDescriptorAllocator();

	ASSERT(pTexHandle->RefCount > 0, "Texture handle reference count is already zero.\n");

	int refCount = --pTexHandle->RefCount;
	if (!refCount)
	{
		if (pTexHandle->pTexResource)
		{
			pTexHandle->pTexResource->Release();
			pTexHandle->pTexResource = nullptr;
		}
		if (pTexHandle->pUploadBuffer)
		{
			pTexHandle->pUploadBuffer->Release();
			pTexHandle->pUploadBuffer = nullptr;
		}
		if (pTexHandle->SRV.ptr)
		{
			pSingleDescriptorAllocator->FreeDescriptorHandle(pTexHandle->SRV);
			pTexHandle->SRV = {};
		}

		if (pTexHandle->pSearchHandle)
		{
			m_pHashTable->Delete(pTexHandle->pSearchHandle);
			pTexHandle->pSearchHandle = nullptr;
		}
		UnLinkFromLinkedList(&m_pTexLinkHead, &m_pTexLinkTail, &pTexHandle->Link);

		delete pTexHandle;
	}
	return refCount;
}
