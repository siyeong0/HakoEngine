#pragma once

#include <unordered_map>
#include <string>

#include "Common/Common.h"
#include "Interface/ITexture.h"

// 전방 선언
class D3D12Renderer;
class D3D12Texture;
class D3D12ResourceManager;
class SingleDescriptorAllocator;
struct ID3D12Device5;
struct ID3D12Resource;

class TextureManager
{
public:
    TextureManager() = default;
    ~TextureManager();

    bool Initialize(D3D12Renderer* pRenderer, int numExpectedItems = 128);
    void Cleanup();

    // 파일 기반 텍스처 (자동 캐싱 + refcount)
    D3D12Texture* CreateTextureFromFile(const wchar_t* wchFileName);

    // CASPER depth atlas (파일 기반, cube R16_UNORM, 자동 캐싱 + refcount)
    D3D12Texture* CreateCasperDepthAtlasTextureFromFile(const wchar_t* wchFileName);

    // 비캐시 텍스처 (사용자가 직접 수명 관리)
    D3D12Texture* CreateDynamicTexture(uint texWidth, uint texHeight, DXGI_FORMAT format = DXGI_FORMAT_R8G8B8A8_UNORM);
    D3D12Texture* CreateImmutableTexture(uint texWidth, uint texHeight, DXGI_FORMAT format, const uint8_t* pInitImage);

    // refcount 감소, 0이면 삭제
    void DeleteTexture(D3D12Texture* pTexture);

private:
    struct TextureRecord
    {
        D3D12Texture* pTexture = nullptr;
        uint32_t refCount = 0;
    };

    using TextureTable = std::unordered_map<std::wstring, TextureRecord>;

private:
    D3D12Texture* createTextureObject() const;

    D3D12Texture* findCachedTexture(const std::wstring& key);
    D3D12Texture* cacheTexture(const std::wstring& key, D3D12Texture* pTex);
    void          releaseTexture(D3D12Texture* pTex);
    void          clearAllRecords();

private:
    D3D12Renderer* m_pRenderer = nullptr;
    D3D12ResourceManager* m_pResourceManager = nullptr;
    ID3D12Device5* m_pDevice = nullptr;
    SingleDescriptorAllocator* m_pSrvAllocator = nullptr;

    TextureTable m_TextureTable;
};

