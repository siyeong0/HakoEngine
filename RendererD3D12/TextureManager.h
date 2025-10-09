#pragma once

class D3D12Renderer;
class D3D12ResourceManager;

class TextureManager
{
public:
	TextureManager() = default;
	~TextureManager() { Cleanup(); }

	bool Initialize(D3D12Renderer* pRenderer, int numExpectedItems);
	TextureHandle* CreateTextureFromFile(const WCHAR* wchFileName);
	TextureHandle* CreateDynamicTexture(uint texWidth, uint texHeight);
	TextureHandle* CreateImmutableTexture(uint texWidth, uint texHeight, DXGI_FORMAT format, const uint8_t* pInitImage);
	void DeleteTexture(TextureHandle* pTexHandle);
	void Cleanup();

private:
	TextureHandle* allocTextureHandle();
	uint freeTextureHandle(TextureHandle* pTexHandle);

private:
	D3D12Renderer* m_pRenderer = nullptr;
	D3D12ResourceManager* m_pResourceManager = nullptr;
	std::unordered_map<std::wstring, TextureHandle*> m_HashTable;
};