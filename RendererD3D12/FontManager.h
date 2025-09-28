#pragma once

class D3D12Renderer;

class FontManager
{
public:
	FontManager() = default;
	~FontManager() { Cleanup(); }

	bool Initialize(D3D12Renderer* pRenderer, ID3D12CommandQueue* pCommandQueue, UINT width, UINT height, bool bEnableDebugLayer);
	FontHandle* CreateFontObject(const WCHAR* wchFontFamilyName, float fontSize);
	void DeleteFontObject(FontHandle* pFontHandle);
	bool WriteTextToBitmap(uint8_t* dstImage, int dstWidth, int dstHeight, int dstPitch, int* outWidth, int* outHeight, FontHandle* pFontHandle, const WCHAR* wchString, int len);
	void Cleanup();

private:
	bool createD2D(ID3D12Device* pD3DDevice, ID3D12CommandQueue* pCommandQueue, bool bEnableDebugLayer);
	bool createDWrite(ID3D12Device* pD3DDevice, UINT texWidth, UINT texHeight, float dpi);
	bool createBitmapFromText(int* outWidth, int* outHeight, IDWriteTextFormat* pTextFormat, const WCHAR* wchString, int len);
	void cleanupDWrite();
	void cleanupD2D();

private:
	D3D12Renderer* m_pRenderer = nullptr;
	ID2D1Device2* m_pD2DDevice = nullptr;
	ID2D1DeviceContext2* m_pD2DDeviceContext = nullptr;

	ID2D1Bitmap1* m_pD2DTargetBitmap = nullptr;
	ID2D1Bitmap1* m_pD2DTargetBitmapRead = nullptr;
	IDWriteFontCollection1* m_pFontCollection = nullptr;
	ID2D1SolidColorBrush* m_pWhiteBrush = nullptr;

	IDWriteFactory5* m_pDWFactory = nullptr;
	DWRITE_LINE_METRICS* m_pLineMetrics = nullptr;
	UINT m_MaxNumLineMetrics = 0;
	UINT m_D2DBitmapWidth = 0;
	UINT m_D2DBitmapHeight = 0;
};
