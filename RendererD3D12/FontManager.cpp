#include "pch.h"

#include "D3D12Renderer.h"

#include "FontManager.h"

using namespace D2D1;

bool FontManager::Initialize(D3D12Renderer* pRenderer, ID3D12CommandQueue* pCommandQueue, UINT width, UINT height, bool bEnableDebugLayer)
{
	ID3D12Device* pD3DDevice = pRenderer->GetD3DDevice();
	createD2D(pD3DDevice, pCommandQueue, bEnableDebugLayer);

	float fDPI = pRenderer->GetDPI();
	createDWrite(pD3DDevice, width, height, fDPI);

	return true;
}

FontHandle* FontManager::CreateFontObject(const WCHAR* wchFontFamilyName, float fontSize)
{
	HRESULT hr = S_OK;

	IDWriteTextFormat* pTextFormat = nullptr;
	IDWriteFactory5* pDWFactory = m_pDWFactory;
	IDWriteFontCollection1* pFontCollection = nullptr;

	// The logical size of the font in DIP("device-independent pixel") units.A DIP equals 1 / 96 inch.

	if (pDWFactory)
	{
		hr = pDWFactory->CreateTextFormat(
			wchFontFamilyName,
			pFontCollection,                       // Font collection (nullptr sets it to use the system font collection).
			DWRITE_FONT_WEIGHT_REGULAR,
			DWRITE_FONT_STYLE_NORMAL,
			DWRITE_FONT_STRETCH_NORMAL,
			fontSize,
			DEFULAT_LOCALE_NAME,
			&pTextFormat);
		ASSERT(SUCCEEDED(hr), "Failed to create IDWriteTextFormat");
	}

	FontHandle* pFontHandle = new FontHandle;
	memset(pFontHandle, 0, sizeof(FontHandle));
	wcscpy_s(pFontHandle->wchFontFamilyName, wchFontFamilyName);
	pFontHandle->FontSize = fontSize;

	if (pTextFormat)
	{
		hr = pTextFormat->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
		ASSERT(SUCCEEDED(hr), "Failed to set text alignment of IDWriteTextFormat");

		hr = pTextFormat->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_NEAR);
		ASSERT(SUCCEEDED(hr), "Failed to set paragraph alignment of IDWriteTextFormat");
	}

	pFontHandle->pTextFormat = pTextFormat;

	return pFontHandle;
}

void FontManager::DeleteFontObject(FontHandle* pFontHandle)
{
	if (pFontHandle->pTextFormat)
	{
		pFontHandle->pTextFormat->Release();
		pFontHandle->pTextFormat = nullptr;
	}
	delete pFontHandle;
}

bool FontManager::WriteTextToBitmap(uint8_t* dstImage, int dstWidth, int dstHeight, int dstPitch, int* outWidth, int* outHeight, FontHandle* pFontHandle, const WCHAR* wchString, int len)
{
	HRESULT hr = S_OK;

	int textWidth = 0;
	int textHeight = 0;

	bool bResult = createBitmapFromText(&textWidth, &textHeight, pFontHandle->pTextFormat, wchString, len);
	if (bResult)
	{
		textWidth = textWidth > dstWidth ? dstWidth : textWidth;
		textHeight = textHeight > dstHeight ? dstHeight : textHeight;

		D2D1_MAPPED_RECT mappedRect;
		hr = m_pD2DTargetBitmapRead->Map(D2D1_MAP_OPTIONS_READ, &mappedRect);
		ASSERT(SUCCEEDED(hr), "Failed to map ID2D1Bitmap1");

		uint8_t* dst = dstImage;
		char* src = (char*)mappedRect.bits;

		for (int y = 0; y < textHeight; y++)
		{
			memcpy(dst, src, textWidth * 4);
			dst += dstPitch;
			src += mappedRect.pitch;
		}
		m_pD2DTargetBitmapRead->Unmap();
	}

	*outWidth = textWidth;
	*outHeight = textHeight;

	return bResult;
}

void FontManager::Cleanup()
{
	cleanupDWrite();
	cleanupD2D();
}

bool FontManager::createD2D(ID3D12Device* pD3DDevice, ID3D12CommandQueue* pCommandQueue, bool bEnableDebugLayer)
{
	// Create D3D11 on D3D12
	// Create an 11 device wrapped around the 12 device and share
	// 12's command queue.
	HRESULT hr = S_OK;

	UINT	d3d11DeviceFlags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;
	D2D1_FACTORY_OPTIONS d2dFactoryOptions = {};

	ID2D1Factory3* pD2DFactory = nullptr;
	ID3D11Device* pD3D11Device = nullptr;
	ID3D11DeviceContext* pD3D11DeviceContext = nullptr;
	ID3D11On12Device* pD3D11On12Device = nullptr;

	if (bEnableDebugLayer)
	{
		d3d11DeviceFlags |= D3D11_CREATE_DEVICE_DEBUG;
	}
	d2dFactoryOptions.debugLevel = D2D1_DEBUG_LEVEL_INFORMATION;

	// Create D3D11 device from D3D12 device.
	hr = D3D11On12CreateDevice(pD3DDevice,
		d3d11DeviceFlags,
		nullptr,
		0,
		(IUnknown**)&pCommandQueue,
		1,
		0,
		&pD3D11Device,
		&pD3D11DeviceContext,
		nullptr);
	ASSERT(SUCCEEDED(hr), "Failed to create ID3D11Device from ID3D12Device");

	hr = pD3D11Device->QueryInterface(IID_PPV_ARGS(&pD3D11On12Device));
	ASSERT(SUCCEEDED(hr), "Failed to get ID3D11On12Device from ID3D11Device");

	// Create D2D/DWrite components.
	D2D1_DEVICE_CONTEXT_OPTIONS deviceOptions = D2D1_DEVICE_CONTEXT_OPTIONS_NONE;
	hr = D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, __uuidof(ID2D1Factory3), &d2dFactoryOptions, (void**)&pD2DFactory);
	ASSERT(SUCCEEDED(hr), "Failed to create ID2D1Factory3");

	// Get the underlying DXGI device of the D3D11 device.
	IDXGIDevice* pDXGIDevice = nullptr;
	hr = pD3D11On12Device->QueryInterface(IID_PPV_ARGS(&pDXGIDevice));
	ASSERT(SUCCEEDED(hr), "Failed to get IDXGIDevice from ID3D11On12Device");
	hr = pD2DFactory->CreateDevice(pDXGIDevice, &m_pD2DDevice);
	ASSERT(SUCCEEDED(hr), "Failed to create ID2D1Device2 from IDXGIDevice");

	// Create a device context.
	hr = m_pD2DDevice->CreateDeviceContext(deviceOptions, &m_pD2DDeviceContext);
	ASSERT(SUCCEEDED(hr), "Failed to create ID2D1DeviceContext2 from ID2D1Device2");

	// Release temporary objects.
	if (pD3D11Device)
	{
		pD3D11Device->Release();
		pD3D11Device = nullptr;
	}
	if (pDXGIDevice)
	{
		pDXGIDevice->Release();
		pDXGIDevice = nullptr;
	}
	if (pD2DFactory)
	{
		pD2DFactory->Release();
		pD2DFactory = nullptr;
	}
	if (pD3D11On12Device)
	{
		pD3D11On12Device->Release();
		pD3D11On12Device = nullptr;
	}
	if (pD3D11DeviceContext)
	{
		pD3D11DeviceContext->Release();
		pD3D11DeviceContext = nullptr;
	}

	return true;
}

bool FontManager::createDWrite(ID3D12Device* pD3DDevice, UINT texWidth, UINT texHeight, float dpi)
{
	HRESULT hr = S_OK;

	m_D2DBitmapWidth = texWidth;
	m_D2DBitmapHeight = texHeight;

	//InitCustomFont(pCustomFontList, dwCustomFontNum);

	D2D1_SIZE_U	size;
	size.width = texWidth;
	size.height = texHeight;

	D2D1_BITMAP_PROPERTIES1 bitmapProperties =
		BitmapProperties1(
			D2D1_BITMAP_OPTIONS_TARGET | D2D1_BITMAP_OPTIONS_CANNOT_DRAW,
			D2D1::PixelFormat(DXGI_FORMAT_R8G8B8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED),
			dpi, dpi);

	hr = m_pD2DDeviceContext->CreateBitmap(size, nullptr, 0, &bitmapProperties, &m_pD2DTargetBitmap);
	ASSERT(SUCCEEDED(hr), "Failed to create ID2D1Bitmap1 from ID2D1DeviceContext2");

	bitmapProperties.bitmapOptions = D2D1_BITMAP_OPTIONS_CANNOT_DRAW | D2D1_BITMAP_OPTIONS_CPU_READ;
	hr = m_pD2DDeviceContext->CreateBitmap(size, nullptr, 0, &bitmapProperties, &m_pD2DTargetBitmapRead);
	ASSERT(SUCCEEDED(hr), "Failed to create ID2D1Bitmap1 from ID2D1DeviceContext2");

	hr = m_pD2DDeviceContext->CreateSolidColorBrush(ColorF(ColorF::White), &m_pWhiteBrush);
	ASSERT(SUCCEEDED(hr), "Failed to create ID2D1SolidColorBrush from ID2D1DeviceContext2");

	hr = DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory5), (IUnknown**)&m_pDWFactory);
	ASSERT(SUCCEEDED(hr), "Failed to create IDWriteFactory5");

	return true;
}

bool FontManager::createBitmapFromText(int* outWidth, int* outHeight, IDWriteTextFormat* pTextFormat, const WCHAR* wchString, int len)
{
	HRESULT hr = S_OK;

	ID2D1DeviceContext* pD2DDeviceContext = m_pD2DDeviceContext;
	IDWriteFactory5* pDWFactory = m_pDWFactory;
	D2D1_SIZE_F max_size = pD2DDeviceContext->GetSize();
	max_size.width = (float)m_D2DBitmapWidth;
	max_size.height = (float)m_D2DBitmapHeight;

	IDWriteTextLayout* textLayout = nullptr;
	if (pDWFactory && pTextFormat)
	{
		hr = pDWFactory->CreateTextLayout(wchString, len, pTextFormat, max_size.width, max_size.height, &textLayout);
		ASSERT(SUCCEEDED(hr), "Failed to create IDWriteTextLayout");
	}

	DWRITE_TEXT_METRICS metrics = {};
	if (textLayout)
	{
		textLayout->GetMetrics(&metrics);
		pD2DDeviceContext->SetTarget(m_pD2DTargetBitmap);
		pD2DDeviceContext->SetTextAntialiasMode(D2D1_TEXT_ANTIALIAS_MODE_GRAYSCALE);

		// Draw the text layout to the D2D surface.
		pD2DDeviceContext->BeginDraw();
		pD2DDeviceContext->Clear(D2D1::ColorF(D2D1::ColorF::Black));
		pD2DDeviceContext->SetTransform(D2D1::Matrix3x2F::Identity());
		pD2DDeviceContext->DrawTextLayout(D2D1::Point2F(0.0f, 0.0f), textLayout, m_pWhiteBrush);

		// We ignore D2DERR_RECREATE_TARGET here. This error indicates that the device
		// is lost. It will be handled during the next call to Present.
		pD2DDeviceContext->EndDraw();

		// Restore default state. 
		pD2DDeviceContext->SetTextAntialiasMode(D2D1_TEXT_ANTIALIAS_MODE_DEFAULT);
		pD2DDeviceContext->SetTarget(nullptr);

		// Release text layout.
		textLayout->Release();
		textLayout = nullptr;
	}
	uint32_t width = (int)ceil(metrics.width);
	uint32_t height = (int)ceil(metrics.height);

	D2D1_POINT_2U destPos = {};
	D2D1_RECT_U	srcRect = { 0, 0, width, height };
	hr = m_pD2DTargetBitmapRead->CopyFromBitmap(&destPos, m_pD2DTargetBitmap, &srcRect);
	ASSERT(SUCCEEDED(hr), "Failed to copy ID2D1Bitmap1");

	*outWidth = width;
	*outHeight = height;

	return true;
}

void FontManager::cleanupDWrite()
{
	if (m_pD2DTargetBitmap)
	{
		m_pD2DTargetBitmap->Release();
		m_pD2DTargetBitmap = nullptr;
	}
	if (m_pD2DTargetBitmapRead)
	{
		m_pD2DTargetBitmapRead->Release();
		m_pD2DTargetBitmapRead = nullptr;
	}
	if (m_pWhiteBrush)
	{
		m_pWhiteBrush->Release();
		m_pWhiteBrush = nullptr;
	}
	if (m_pDWFactory)
	{
		m_pDWFactory->Release();
		m_pDWFactory = nullptr;
	}
}

void FontManager::cleanupD2D()
{
	if (m_pD2DDeviceContext)
	{
		m_pD2DDeviceContext->Release();
		m_pD2DDeviceContext = nullptr;
	}
	if (m_pD2DDevice)
	{
		m_pD2DDevice->Release();
		m_pD2DDevice = nullptr;
	}
}