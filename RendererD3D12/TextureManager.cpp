#include "pch.h"
#include "SingleDescriptorAllocator.h"
#include "D3D12Renderer.h"
#include "D3D12ResourceManager.h"
#include "TextureManager.h"

bool TextureManager::Initialize(D3D12Renderer* pRenderer, int numExpectedItems)
{
	m_pRenderer = pRenderer;
	m_pResourceManager = pRenderer->GetResourceManager();

	m_HashTable.reserve(numExpectedItems);

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
	TextureHandle* pOutTexHandle = nullptr;

	if (auto it = m_HashTable.find(wchFileName); it != m_HashTable.end())
	{
		pOutTexHandle = it->second;
		++it->second->RefCount;
	}
	else
	{
		if (m_pResourceManager->CreateTextureFromFile(&pTexResource, &desc, wchFileName, bUseGpuUploadHeaps))
		{
			D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
			srvDesc.Format = desc.Format;
			srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;

			if (desc.Dimension == D3D12_RESOURCE_DIMENSION_TEXTURE2D)
			{
				if (desc.DepthOrArraySize > 1)
				{
					if (desc.DepthOrArraySize == 6 && (desc.Flags & D3D12_RESOURCE_FLAG_ALLOW_SIMULTANEOUS_ACCESS) == 0)
					{
						// CubeMap
						srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURECUBE;
						srvDesc.TextureCube.MipLevels = desc.MipLevels;
					}
					else
					{
						// 2D Array
						srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2DARRAY;
						srvDesc.Texture2DArray.MipLevels = desc.MipLevels;
						srvDesc.Texture2DArray.ArraySize = desc.DepthOrArraySize;
					}
				}
				else
				{
					// Normal 2D texture
					srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
					srvDesc.Texture2D.MipLevels = desc.MipLevels;
				}
			}
			else if (desc.Dimension == D3D12_RESOURCE_DIMENSION_TEXTURE3D)
			{
				srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE3D;
				srvDesc.Texture2D.MipLevels = desc.MipLevels;
			}
			else
			{
				ASSERT(false, "Unsupported texture type.\n");
			}

			// Descriptor heap allocation and SRV creation
			if (pSingleDescriptorAllocator->AllocDescriptorHandle(&srv))
			{
				pD3DDevice->CreateShaderResourceView(pTexResource, &srvDesc, srv);

				pOutTexHandle = allocTextureHandle();
				pOutTexHandle->pTexResource = pTexResource;
				pOutTexHandle->bFromFile = TRUE;
				pOutTexHandle->SRV = srv;
				pOutTexHandle->Dimension = srvDesc.ViewDimension;

				auto bResult = m_HashTable.insert({ wchFileName,  pOutTexHandle }).second;
				ASSERT(bResult, "HashTable insertion failed.\n");
			}
			else
			{
				pTexResource->Release();
				pTexResource = nullptr;
			}
		}
	}

	return pOutTexHandle;
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
	ASSERT(m_HashTable.size() > 0, "Texture resource leak detected.\n");
	m_HashTable.clear();
}

TextureHandle* TextureManager::allocTextureHandle()
{
	TextureHandle* pTexHandle = new TextureHandle;
	memset(pTexHandle, 0, sizeof(TextureHandle));
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

		delete pTexHandle;
	}
	return refCount;
}
