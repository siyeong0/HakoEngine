#pragma once

class CHashTable;
class D3D12Renderer;
class D3D12ResourceManager;

class TextureManager
{
public:
	TextureManager() = default;
	~TextureManager() { Cleanup(); }

	bool Initialize(D3D12Renderer* pRenderer, UINT maxNumBuckets, UINT maxNumFiles);
	TextureHandle* CreateTextureFromFile(const WCHAR* wchFileName);
	TextureHandle* CreateDynamicTexture(UINT texWidth, UINT texHeight);
	TextureHandle* CreateImmutableTexture(UINT texWidth, UINT texHeight, DXGI_FORMAT format, const uint8_t* pInitImage);
	void DeleteTexture(TextureHandle* pTexHandle);
	void Cleanup();

private:
	TextureHandle* allocTextureHandle();
	UINT freeTextureHandle(TextureHandle* pTexHandle);

private:
	D3D12Renderer* m_pRenderer = nullptr;
	D3D12ResourceManager* m_pResourceManager = nullptr;
	CHashTable* m_pHashTable = nullptr;

	SORT_LINK* m_pTexLinkHead = nullptr;
	SORT_LINK* m_pTexLinkTail = nullptr;
};