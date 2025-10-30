// Image.cpp — Implementation for the provided Image.h
// Platform focus: Windows (_WIN32) with DirectXTex. Non-Windows stubs return false.
// Requires: vcpkg: directxtex

#include "pch.h"
#include "Image.h"
#include "Color.h"

#include <algorithm>
#include <cstring>
#include <functional>
#include <iostream>

#ifdef _WIN32
#include <DirectXTex.h>
#include <wincodec.h>   // WIC
#include <ocidl.h>      // IPropertyBag2 (JPEG quality)
#endif

#ifdef _WIN32
static_assert(Image::FORMAT_R8G8B8A8_UNORM == 28, "DXGI numeric mismatch");
static_assert(Image::FORMAT_BC7_UNORM == 98, "DXGI numeric mismatch");
#endif

// -------------------------------------
// Local helpers (no pimpl/ensure helpers)
// -------------------------------------
#ifdef _WIN32
namespace
{
	// Internal state stored in m_pData (lifetime: ctor→dtor; Reset only releases content)
	struct ImageState { DirectX::ScratchImage scratch; };

	inline Image::EImageFormat GuessFromExt(const std::filesystem::path& p)
	{
		auto ext = p.extension().wstring();
		std::transform(ext.begin(), ext.end(), ext.begin(), ::towlower);

		if (ext == L".dds") return Image::EImageFormat::DDS;  // ← 변경점
		if (ext == L".png") return Image::EImageFormat::PNG;
		if (ext == L".jpg" || ext == L".jpeg") return Image::EImageFormat::JPEG;
		if (ext == L".bmp") return Image::EImageFormat::BMP;
		if (ext == L".tif" || ext == L".tiff") return Image::EImageFormat::TIFF;
		if (ext == L".gif") return Image::EImageFormat::GIF;
		if (ext == L".wdp" || ext == L".jxr" || ext == L".wmp" || ext == L".hdp") return Image::EImageFormat::WMP;
		if (ext == L".tga") return Image::EImageFormat::TGA;
		if (ext == L".hdr") return Image::EImageFormat::HDR;

		return Image::EImageFormat::PNG; // default
	}

	inline DirectX::WICCodecs CodecFromFormat(Image::EImageFormat f)
	{
		using F = Image::EImageFormat;
		switch (f)
		{
		case F::PNG:  return DirectX::WIC_CODEC_PNG;
		case F::JPEG: return DirectX::WIC_CODEC_JPEG;
		case F::BMP:  return DirectX::WIC_CODEC_BMP;
		case F::TIFF: return DirectX::WIC_CODEC_TIFF;
		case F::GIF:  return DirectX::WIC_CODEC_GIF;
		case F::WMP:  return DirectX::WIC_CODEC_WMP;
		default:      return DirectX::WIC_CODEC_PNG;
		}
	}

	inline Image::FORMAT ToSRGB(Image::FORMAT f)
	{
		// Map a small set commonly used; other values left as-is (numeric DXGI-compatible)
		switch (static_cast<DXGI_FORMAT>(f))
		{
		case DXGI_FORMAT_R8G8B8A8_UNORM: return static_cast<Image::FORMAT>(DXGI_FORMAT_R8G8B8A8_UNORM_SRGB);
		case DXGI_FORMAT_B8G8R8A8_UNORM: return static_cast<Image::FORMAT>(DXGI_FORMAT_B8G8R8A8_UNORM_SRGB);
		case DXGI_FORMAT_B8G8R8X8_UNORM: return static_cast<Image::FORMAT>(DXGI_FORMAT_B8G8R8X8_UNORM_SRGB);
		case DXGI_FORMAT_BC1_UNORM:      return static_cast<Image::FORMAT>(DXGI_FORMAT_BC1_UNORM_SRGB);
		case DXGI_FORMAT_BC2_UNORM:      return static_cast<Image::FORMAT>(DXGI_FORMAT_BC2_UNORM_SRGB);
		case DXGI_FORMAT_BC3_UNORM:      return static_cast<Image::FORMAT>(DXGI_FORMAT_BC3_UNORM_SRGB);
		case DXGI_FORMAT_BC7_UNORM:      return static_cast<Image::FORMAT>(DXGI_FORMAT_BC7_UNORM_SRGB);
		default: return f;
		}
	}

	inline Image::DDSHeader MakeDDSHeader(const DirectX::TexMetadata& md)
	{
		Image::DDSHeader d = {};
		d.Width = static_cast<uint>(md.width);
		d.Height = static_cast<uint>(md.height);
		d.MipLevels = static_cast<uint>(md.mipLevels ? md.mipLevels : 1);
		d.ArraySize = static_cast<uint>(md.arraySize ? md.arraySize : 1);
		d.bCubeMap = (md.miscFlags & DirectX::TEX_MISC_TEXTURECUBE) != 0;
		d.Format = static_cast<Image::FORMAT>(md.format);
		return d;
	}

	inline Image::GeneralHeader MakeGeneralHeader(const DirectX::TexMetadata& md)
	{
		Image::GeneralHeader g{};
		g.Width = static_cast<uint>(md.width);
		g.Height = static_cast<uint>(md.height);
		uint bpp = static_cast<uint>(DirectX::BitsPerPixel(md.format));
		g.Channels = (bpp % 8 == 0) ? std::max<uint>(1, bpp / 8) : 4;
		g.Format = static_cast<Image::FORMAT>(md.format);
		return g;
	}

	inline bool HeuristicIsDDS(const DirectX::TexMetadata& md)
	{
		return md.mipLevels > 1 || md.arraySize > 1 ||
			(md.miscFlags & DirectX::TEX_MISC_TEXTURECUBE) != 0 ||
			DirectX::IsCompressed(md.format);
	}

	inline void UpdateHeaderVariant(Image::Header& dst, const DirectX::TexMetadata& md)
	{
		if (HeuristicIsDDS(md))
		{
			dst = MakeDDSHeader(md);
		}
		else
		{
			dst = MakeGeneralHeader(md);
		}
	}

	inline uint8_t f2u8(float v)
	{
		v = std::clamp(v, 0.0f, 1.0f);
		return static_cast<uint8_t>(v * 255.0f + 0.5f);
	}

}

namespace 
{
	// Thread-local COM init for WIC. Never uninitialize to keep WIC factory valid.
	inline void EnsureCOMForWIC()
	{
		static thread_local bool s_inited = false;
		if (!s_inited)
		{
			HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
			// If another mode was already set (STA), we still proceed; WIC factory may still work.
			if (FAILED(hr) && hr != RPC_E_CHANGED_MODE) 
			{
				// Optional: your ASSERT/log
				// ASSERT(false, "CoInitializeEx failed for WIC");
			}
			s_inited = true;
		}
	}
}
#endif // _WIN32

// -------------------------------------
// Ctors / Dtor / Assignments
// -------------------------------------
Image::Image()
{
#ifdef _WIN32
	m_pData = reinterpret_cast<uint8_t*>(new ImageState{});
#endif
}

Image::~Image()
{
	Reset();
	m_Header = GeneralHeader{};
}

Image::Image(Image&& rhs) noexcept
{
	m_Header = std::move(rhs.m_Header);
	m_pData = rhs.m_pData;
	rhs.m_pData = nullptr;
	ZeroMemory(&rhs.m_Header, sizeof(rhs.m_Header));
}

Image& Image::operator=(Image&& rhs) noexcept
{
	if (this != &rhs) {
		Reset();
		m_pData = rhs.m_pData;
		m_Header = rhs.m_Header;
		rhs.m_pData = nullptr;
		ZeroMemory(&rhs.m_Header, sizeof(rhs.m_Header));
	}
	return *this;
}

// -------------------------------------
// Load
// -------------------------------------
bool Image::Load(const std::filesystem::path& path)
{
#ifdef _WIN32
	ASSERT(!path.empty(), "Image::Load - empty path");

	if (!m_pData)
	{
		m_pData = reinterpret_cast<uint8_t*>(new ImageState{});
	}
	ImageState* st = reinterpret_cast<ImageState*>(m_pData);

	DirectX::ScratchImage img;
	DirectX::TexMetadata md = {};

	const EImageFormat f = GuessFromExt(path);
	HRESULT hr = E_FAIL;

	switch (f)
	{
	case EImageFormat::DDS:
		hr = DirectX::LoadFromDDSFile(path.c_str(), DirectX::DDS_FLAGS_NONE, &md, img);
		break;
	case EImageFormat::TGA:
		hr = DirectX::LoadFromTGAFile(path.c_str(), &md, img);
		break;
	case EImageFormat::HDR:
		hr = DirectX::LoadFromHDRFile(path.c_str(), &md, img);
		break;
	case EImageFormat::Auto:
	case EImageFormat::PNG:
	case EImageFormat::JPEG:
	case EImageFormat::BMP:
	case EImageFormat::TIFF:
	case EImageFormat::GIF:
	case EImageFormat::WMP:
		EnsureCOMForWIC();
		hr = DirectX::LoadFromWICFile(path.c_str(), DirectX::WIC_FLAGS_NONE, &md, img);
		break;
	default:
		break;
	}

	if (FAILED(hr))
	{
		std::wcerr << L"[Image] Load failed: " << path.wstring() << L"\n";
		return false;
	}

	st->scratch = std::move(img);
	UpdateHeaderVariant(m_Header, md);
	return true;
#else
	(void)path; return false;
#endif
}


bool Image::LoadFromMemory(const void* data, size_t sizeBytes, std::string_view hintExtension)
{
#ifdef _WIN32
	ASSERT(data && sizeBytes, "Image::LoadFromMemory - invalid buffer");
	if (!data || !sizeBytes)
	{
		return false;
	}

	if (!m_pData)
	{
		m_pData = reinterpret_cast<uint8_t*>(new ImageState{});
	}
	ImageState* st = reinterpret_cast<ImageState*>(m_pData);

	DirectX::ScratchImage img; 
	DirectX::TexMetadata md{};
	HRESULT hr = E_FAIL;

	// Try DDS → WIC → HDR → TGA (no reliance on hint)
	hr = DirectX::LoadFromDDSMemory(reinterpret_cast<const uint8_t*>(data), sizeBytes, DirectX::DDS_FLAGS_NONE, &md, img);
	if (SUCCEEDED(hr))
	{
		goto lb_return;
	}
	EnsureCOMForWIC();
	hr = DirectX::LoadFromWICMemory(reinterpret_cast<const uint8_t*>(data), sizeBytes, DirectX::WIC_FLAGS_NONE, &md, img);
	if (SUCCEEDED(hr))
	{
		goto lb_return;
	}
	hr = DirectX::LoadFromHDRMemory(reinterpret_cast<const std::byte*>(data), sizeBytes, &md, img);
	if (SUCCEEDED(hr))
	{
		goto lb_return;
	}
	hr = DirectX::LoadFromTGAMemory(reinterpret_cast<const uint8_t*>(data), sizeBytes, &md, img);
	if (SUCCEEDED(hr))
	{
		goto lb_return;
	}

	ASSERT(FAILED(hr), "Image::LoadFromMemory - failed to load image from memory");
	return false;

lb_return:
	st->scratch = std::move(img);
	UpdateHeaderVariant(m_Header, md);
	(void)hintExtension;

	return true;
#else
	(void)data; (void)sizeBytes; (void)hintExtension; return false;
#endif
}

bool Image::LoadFromMemory(const RGBA* data, size_t sizeBytes, std::string_view hintExtension)
{
	return LoadFromMemory(reinterpret_cast<const void*>(data), sizeBytes, hintExtension);
}

bool Image::LoadFromMemory(const Color* data, size_t sizeBytes, std::string_view hintExtension)
{
	return LoadFromMemory(reinterpret_cast<const void*>(data), sizeBytes, hintExtension);
}

bool Image::Load(const std::filesystem::path& path, Image* out)
{
	ASSERT(out, "Image::Load(out) - out is null");
	if (!out)
	{
		return false;
	}
	return out->Load(path);
}

bool Image::LoadFromMemory(const void* data, size_t sizeBytes, Image* out, std::string_view hintExtension)
{
	ASSERT(out, "Image::LoadFromMemory(out) - out is null");
	if (!out)
	{
		return false;
	}
	return out->LoadFromMemory(data, sizeBytes, hintExtension);
}

// -------------------------------------
// Creation
// -------------------------------------
Image Image::CreateBlank(uint w, uint h, FORMAT fmt, uint mips, uint arraySize, bool bCubeMap)
{
	Image out;
#ifdef DXTEX
	if (!out.m_pData)
	{
		out.m_pData = reinterpret_cast<uint8_t*>(new ImageState{});
	}
	ImageState* st = reinterpret_cast<ImageState*>(out.m_pData);

	DirectX::TexMetadata md = {};
	md.width = w; 
	md.height = h; 
	md.depth = 1;
	md.arraySize = arraySize ? arraySize : 1;
	md.mipLevels = std::max<uint>(1, mips);
	md.format = static_cast<DXGI_FORMAT>(fmt);
	md.dimension = DirectX::TEX_DIMENSION_TEXTURE2D;
	if (bCubeMap)
	{
		md.miscFlags |= DirectX::TEX_MISC_TEXTURECUBE; md.arraySize = 6;
	}

	DirectX::ScratchImage tmp;
	if (FAILED(tmp.Initialize(md)))
	{
		out.Reset();
		return out;
	}

	for (size_t i = 0; i < tmp.GetImageCount(); ++i)
	{
		std::memset(tmp.GetImages()[i].pixels, 0, tmp.GetImages()[i].slicePitch);
	}

	st->scratch = std::move(tmp);
	UpdateHeaderVariant(out.m_Header, md);
#endif
	return out;
}

Image Image::CreateSolidColor(uint w, uint h, const uint8_t rgba[4], FORMAT fmt, uint mips, uint arraySize, bool bCubeMap)
{
	Image out;
#ifdef DXTEX
	if (!out.m_pData)
	{
		out.m_pData = reinterpret_cast<uint8_t*>(new ImageState{});
	}
	ImageState* st = reinterpret_cast<ImageState*>(out.m_pData);

	DirectX::TexMetadata md = {};
	md.width = w; md.height = h; md.depth = 1;
	md.arraySize = bCubeMap ? 6u : (arraySize ? arraySize : 1u);
	md.mipLevels = std::max<uint>(1, mips);
	md.format = DXGI_FORMAT_R8G8B8A8_UNORM;
	md.dimension = DirectX::TEX_DIMENSION_TEXTURE2D;
	md.miscFlags |= bCubeMap ? DirectX::TEX_MISC_TEXTURECUBE : 0;

	DirectX::ScratchImage img;
	if (FAILED(img.Initialize(md)))
	{
		out.Reset(); return out;
	}

	for (size_t i = 0; i < img.GetImageCount(); ++i)
	{
		const DirectX::Image* im = img.GetImages() + i;
		for (size_t y = 0; y < im->height; ++y)
		{
			auto* row = im->pixels + y * im->rowPitch;
			for (size_t x = 0; x < im->width; ++x)
			{
				row[x * 4 + 0] = rgba[0]; row[x * 4 + 1] = rgba[1]; row[x * 4 + 2] = rgba[2]; row[x * 4 + 3] = rgba[3];
			}
		}
	}

	// Convert/compress to target format if needed
	const auto target = static_cast<DXGI_FORMAT>(fmt);
	if (target != DXGI_FORMAT_R8G8B8A8_UNORM)
	{
		if (DirectX::IsCompressed(target))
		{
			DirectX::ScratchImage comp;
			if (FAILED(DirectX::Compress(img.GetImages(), img.GetImageCount(), md, target, DirectX::TEX_COMPRESS_DEFAULT, DirectX::TEX_THRESHOLD_DEFAULT, comp)))
			{
				out.Reset();
				return out;
			}
			img = std::move(comp);
		}
		else
		{
			DirectX::ScratchImage conv;
			if (FAILED(DirectX::Convert(img.GetImages(), img.GetImageCount(), md, target, DirectX::TEX_FILTER_DEFAULT, DirectX::TEX_THRESHOLD_DEFAULT, conv)))
			{
				out.Reset(); return out;
			}
			img = std::move(conv);
		}
	}

	st->scratch = std::move(img);
	UpdateHeaderVariant(out.m_Header, st->scratch.GetMetadata());
#endif
	return out;
}

Image Image::CreateSolidColor(uint w, uint h, const RGBA& rgba, FORMAT fmt, uint mips, uint arraySize, bool bCubeMap)
{
	const uint8_t c[4] = { rgba.r, rgba.g, rgba.b, rgba.a };
	return CreateSolidColor(w, h, c, fmt, mips, arraySize, bCubeMap);
}

Image Image::CreateSolidColor(uint w, uint h, const Color& color, FORMAT fmt, uint mips, uint arraySize, bool bCubeMap)
{
	const uint8_t c[4] = { f2u8(color.r), f2u8(color.g), f2u8(color.b), f2u8(color.a) };
	return CreateSolidColor(w, h, c, fmt, mips, arraySize, bCubeMap);
}

// -------------------------------------
// Introspection
// -------------------------------------
bool Image::IsValid() const
{
#ifdef DXTEX
	const ImageState* st = reinterpret_cast<const ImageState*>(m_pData);
	return st && st->scratch.GetImageCount() > 0;
#else
	return false;
#endif
}

bool Image::IsDDS() const
{
#ifdef DXTEX
	const ImageState* st = reinterpret_cast<const ImageState*>(m_pData);
	if (!st)
	{
		return false;
	}
	return HeuristicIsDDS(st->scratch.GetMetadata());
#else
	return false;
#endif
}

Image::Header Image::GetHeader() const
{
#ifdef DXTEX
	const ImageState* st = reinterpret_cast<const ImageState*>(m_pData);
	if (!st)
	{
		return GeneralHeader{};
	}
	Image::Header h;
	UpdateHeaderVariant(h, st->scratch.GetMetadata());
	return h;
#else
	return GeneralHeader{};
#endif
}

Image::FORMAT Image::GetFormat() const
{
#ifdef DXTEX
	const ImageState* st = reinterpret_cast<const ImageState*>(m_pData);
	return st ? static_cast<Image::FORMAT>(st->scratch.GetMetadata().format) : FORMAT_UNKNOWN;
#else
	return FORMAT_UNKNOWN;
#endif
}

uint Image::GetWidth() const
{
#ifdef DXTEX
	const ImageState* st = reinterpret_cast<const ImageState*>(m_pData);
	return st ? static_cast<uint>(st->scratch.GetMetadata().width) : 0;
#else
	return 0;
#endif
}

uint Image::GetHeight() const
{
#ifdef DXTEX
	const ImageState* st = reinterpret_cast<const ImageState*>(m_pData);
	return st ? static_cast<uint>(st->scratch.GetMetadata().height) : 0;
#else
	return 0;
#endif
}

uint Image::GetChannels() const
{
#ifdef DXTEX
	const ImageState* st = reinterpret_cast<const ImageState*>(m_pData);
	if (!st)
	{
		return 0;
	}
	size_t bpp = static_cast<uint>(DirectX::BitsPerPixel(st->scratch.GetMetadata().format));
	return (bpp % 8 == 0) ? std::max<uint>(1, bpp / 8) : 0;
#else
	return 0;
#endif
}

uint Image::GetMipCount() const
{
#ifdef DXTEX
	const ImageState* st = reinterpret_cast<const ImageState*>(m_pData);
	return st ? static_cast<uint>(st->scratch.GetMetadata().mipLevels) : 0;
#else
	return 0;
#endif
}

uint Image::GetArraySize() const
{
#ifdef DXTEX
	const ImageState* st = reinterpret_cast<const ImageState*>(m_pData);
	return st ? static_cast<uint>(st->scratch.GetMetadata().arraySize) : 0;
#else
	return 0;
#endif
}

bool Image::IsCubeMap() const
{
#ifdef DXTEX
	const ImageState* st = reinterpret_cast<const ImageState*>(m_pData);
	return st && ((st->scratch.GetMetadata().miscFlags & DirectX::TEX_MISC_TEXTURECUBE) != 0);
#else
	return false;
#endif
}

// -------------------------------------
// Subresource access
// -------------------------------------
const void* Image::GetDataPtr(uint item, uint mip, uint slice) const
{
#ifdef DXTEX
	const ImageState* st = reinterpret_cast<const ImageState*>(m_pData);
	if (!st)
	{
		return nullptr;
	}
	const DirectX::Image* im = st->scratch.GetImage(mip, item, slice);
	return im ? im->pixels : nullptr;
#else
	(void)item; (void)mip; (void)slice; return nullptr;
#endif
}

size_t Image::GetRowPitch(uint item, uint mip, uint slice) const
{
#ifdef DXTEX
	const ImageState* st = reinterpret_cast<const ImageState*>(m_pData);
	if (!st)
	{
		return 0;
	}
	const DirectX::Image* im = st->scratch.GetImage(mip, item, slice);
	return im ? im->rowPitch : 0;
#else
	(void)item; (void)mip; (void)slice; return 0;
#endif
}

size_t Image::GetSlicePitch(uint item, uint mip, uint slice) const
{
#ifdef DXTEX
	const ImageState* st = reinterpret_cast<const ImageState*>(m_pData);
	if (!st)
	{
		return 0;
	}
	const DirectX::Image* im = st->scratch.GetImage(mip, item, slice);
	return im ? im->slicePitch : 0;
#else
	(void)item; (void)mip; (void)slice; return 0;
#endif
}

std::vector<uint8_t> Image::CopyTopMipAsRGBA8() const
{
	std::vector<uint8_t> out;
#ifdef DXTEX
	const ImageState* st = reinterpret_cast<const ImageState*>(m_pData);
	if (!st)
	{
		return out;
	}

	const DirectX::TexMetadata& md = st->scratch.GetMetadata();
	const DirectX::Image* top = st->scratch.GetImage(0, 0, 0);
	if (!top)
	{
		return out;
	}

	if (md.format == DXGI_FORMAT_R8G8B8A8_UNORM || md.format == DXGI_FORMAT_R8G8B8A8_UNORM_SRGB)
	{
		out.resize(top->rowPitch * top->height);
		for (size_t y = 0; y < top->height; ++y)
		{
			std::memcpy(out.data() + y * top->rowPitch, top->pixels + y * top->rowPitch, top->rowPitch);
		}
		return out;
	}

	DirectX::ScratchImage conv;
	if (FAILED(DirectX::Convert(st->scratch.GetImages(), st->scratch.GetImageCount(), md, DXGI_FORMAT_R8G8B8A8_UNORM, DirectX::TEX_FILTER_DEFAULT, DirectX::TEX_THRESHOLD_DEFAULT, conv)))
	{
		return out;
	}

	const DirectX::Image* im = conv.GetImage(0, 0, 0);
	out.resize(im->rowPitch * im->height);
	for (size_t y = 0; y < im->height; ++y)
	{
		std::memcpy(out.data() + y * im->rowPitch, im->pixels + y * im->rowPitch, im->rowPitch);
	}

#endif
	return out;
}

// -------------------------------------
// Editing / processing
// -------------------------------------
bool Image::Convert(FORMAT newFmt, bool bUseSRGB)
{
#ifdef DXTEX
	ImageState* st = reinterpret_cast<ImageState*>(m_pData);
	if (!st || st->scratch.GetImageCount() == 0)
	{
		return false;
	}

	FORMAT fmt = bUseSRGB ? ToSRGB(newFmt) : newFmt;

	DirectX::ScratchImage dst;
	const DirectX::TexMetadata& md = st->scratch.GetMetadata();
	HRESULT hr = DirectX::Convert(st->scratch.GetImages(), st->scratch.GetImageCount(), md, static_cast<DXGI_FORMAT>(fmt), DirectX::TEX_FILTER_DEFAULT, DirectX::TEX_THRESHOLD_DEFAULT, dst);

	if (FAILED(hr))
	{
		return false;
	}

	st->scratch = std::move(dst);
	UpdateHeaderVariant(m_Header, st->scratch.GetMetadata());

	return true;
#else
	(void)newFmt; (void)bUseSRGB; return false;
#endif
}

bool Image::Resize(uint width, uint height, bool bUseSRGB)
{
#ifdef DXTEX
	ImageState* st = reinterpret_cast<ImageState*>(m_pData);
	if (!st || st->scratch.GetImageCount() == 0)
	{
		return false;
	}

	DirectX::ScratchImage dst;
	const DirectX::TexMetadata& md = st->scratch.GetMetadata();

	DirectX::TEX_FILTER_FLAGS filter = DirectX::TEX_FILTER_DEFAULT;
	if (bUseSRGB)
	{
		filter = static_cast<DirectX::TEX_FILTER_FLAGS>(filter | DirectX::TEX_FILTER_SRGB);
	}

	HRESULT hr = DirectX::Resize(st->scratch.GetImages(), st->scratch.GetImageCount(), md, size_t(width), size_t(height), filter, dst);

	if (FAILED(hr))
	{
		return false;
	}

	st->scratch = std::move(dst);
	UpdateHeaderVariant(m_Header, st->scratch.GetMetadata());

	return true;
#else
	(void)width; (void)height; (void)bUseSRGB; return false;
#endif
}


bool Image::GenerateMips(uint desiredMipCount, uint filter, bool bUseSRGB)
{
#ifdef DXTEX
	ImageState* st = reinterpret_cast<ImageState*>(m_pData);
	if (!st || st->scratch.GetImageCount() == 0)
	{
		return false;
	}
	DirectX::ScratchImage dst;
	const DirectX::TexMetadata& md = st->scratch.GetMetadata();
	DirectX::TEX_FILTER_FLAGS f = static_cast<DirectX::TEX_FILTER_FLAGS>(filter ? filter : DirectX::TEX_FILTER_DEFAULT);

	if (bUseSRGB)
	{
		f = static_cast<DirectX::TEX_FILTER_FLAGS>(f | DirectX::TEX_FILTER_SRGB);
	}

	HRESULT hr = DirectX::GenerateMipMaps(st->scratch.GetImages(), st->scratch.GetImageCount(), md, f, size_t(desiredMipCount), dst);

	if (FAILED(hr))
	{
		return false;
	}

	st->scratch = std::move(dst);
	UpdateHeaderVariant(m_Header, st->scratch.GetMetadata());

	return true;
#else
	(void)desiredMipCount; (void)filter; (void)bUseSRGB; return false;
#endif
}

bool Image::FlipY()
{
#ifdef DXTEX
	ImageState* st = reinterpret_cast<ImageState*>(m_pData);
	if (!st || st->scratch.GetImageCount() == 0)
	{
		return false;
	}

	DirectX::ScratchImage dst;
	const DirectX::TexMetadata& md = st->scratch.GetMetadata();

	HRESULT hr = DirectX::FlipRotate(st->scratch.GetImages(), st->scratch.GetImageCount(), md, DirectX::TEX_FR_FLIP_VERTICAL, dst);

	if (FAILED(hr))
	{
		return false;
	}

	st->scratch = std::move(dst);
	return true;
#else
	return false;
#endif
}

bool Image::Decompress()
{
#ifdef DXTEX
	ImageState* st = reinterpret_cast<ImageState*>(m_pData);
	if (!st || st->scratch.GetImageCount() == 0)
	{
		return false;
	}

	const DirectX::TexMetadata& md = st->scratch.GetMetadata();

	if (!DirectX::IsCompressed(md.format))
	{
		return true;
	}

	DirectX::ScratchImage dst;
	HRESULT hr = DirectX::Decompress(st->scratch.GetImages(), st->scratch.GetImageCount(), md, DXGI_FORMAT_R8G8B8A8_UNORM, dst);

	if (FAILED(hr))
	{
		return false;
	}

	st->scratch = std::move(dst);
	UpdateHeaderVariant(m_Header, st->scratch.GetMetadata());

	return true;
#else
	return false;
#endif
}

bool Image::Compress(FORMAT target, unsigned long flags, bool bUseSRGB)
{
#ifdef DXTEX
	ImageState* st = reinterpret_cast<ImageState*>(m_pData);
	if (!st || st->scratch.GetImageCount() == 0)
	{
		return false;
	}

	const DXGI_FORMAT tgt = static_cast<DXGI_FORMAT>(bUseSRGB ? ToSRGB(target) : target);

	if (!DirectX::IsCompressed(tgt))
	{
		return false;
	}

	DirectX::ScratchImage dst;
	const DirectX::TexMetadata& md = st->scratch.GetMetadata();
	HRESULT hr = DirectX::Compress(st->scratch.GetImages(), st->scratch.GetImageCount(), md, tgt, static_cast<DirectX::TEX_COMPRESS_FLAGS>(flags), DirectX::TEX_THRESHOLD_DEFAULT, dst);

	if (FAILED(hr))
	{
		return false;
	}

	st->scratch = std::move(dst);
	UpdateHeaderVariant(m_Header, st->scratch.GetMetadata());

	return true;
#else
	(void)target; (void)flags; (void)bUseSRGB; return false;
#endif
}

// -------------------------------------
// Save
// -------------------------------------
bool Image::Save(
	const std::filesystem::path& path,
	EImageFormat format,
	bool bGenerateMips,
	size_t mipLevels) const
{
#ifdef DXTEX
	const ImageState* st = reinterpret_cast<const ImageState*>(m_pData);
	if (!st || st->scratch.GetImageCount() == 0)
	{
		return false;
	}

	const EImageFormat fmt = (format == EImageFormat::Auto) ? GuessFromExt(path) : format;

	if (fmt == EImageFormat::DDS)
	{
		const DirectX::Image* images = st->scratch.GetImages();
		size_t n = st->scratch.GetImageCount();
		DirectX::TexMetadata md = st->scratch.GetMetadata();

		DirectX::ScratchImage mipped;
		if (bGenerateMips)
		{
			if (FAILED(DirectX::GenerateMipMaps(images, n, md, DirectX::TEX_FILTER_DEFAULT, mipLevels, mipped)))
			{
				return false;
			}
			images = mipped.GetImages();
			n = mipped.GetImageCount();
			md = mipped.GetMetadata();
		}

		return SUCCEEDED(DirectX::SaveToDDSFile(images, n, md, DirectX::DDS_FLAGS_NONE, path.c_str()));
	}

	if (fmt == EImageFormat::HDR)
	{
		const DirectX::Image* base = st->scratch.GetImage(0, 0, 0);
		ASSERT(base, "Image::Save HDR - missing base image");
		return base && SUCCEEDED(DirectX::SaveToHDRFile(*base, path.c_str()));
	}

	if (fmt == EImageFormat::TGA)
	{
		const DirectX::Image* base = st->scratch.GetImage(0, 0, 0);
		ASSERT(base, "Image::Save TGA - missing base image");
		return base && SUCCEEDED(DirectX::SaveToTGAFile(*base, path.c_str()));
	}

	// ---- WIC container (PNG/JPEG/BMP/TIFF/GIF/WMP) ----
	const DirectX::Image* toSave = st->scratch.GetImage(0, 0, 0);
	DirectX::ScratchImage temp;
	const DirectX::TexMetadata& md = st->scratch.GetMetadata();

	if (DirectX::IsCompressed(md.format) ||
		(md.format != DXGI_FORMAT_R8G8B8A8_UNORM && md.format != DXGI_FORMAT_B8G8R8A8_UNORM && md.format != DXGI_FORMAT_B8G8R8X8_UNORM))
	{
		if (FAILED(DirectX::Decompress(toSave, 1, md, DXGI_FORMAT_R8G8B8A8_UNORM, temp)))
		{
			return false;
		}
		toSave = temp.GetImages();
	}

	// JPEG quality (IPropertyBag2)
	std::function<void(IPropertyBag2*)> setProps = nullptr;
	if (fmt == EImageFormat::JPEG)
	{
		const float q = std::clamp(GetJPEGQuality(), 0.0f, 1.0f);
		setProps = [q](IPropertyBag2* bag)
			{
				if (!bag)
				{
					return;
				}
				PROPBAG2 opt{}; opt.pstrName = const_cast<wchar_t*>(L"ImageQuality");
				VARIANT v; VariantInit(&v); v.vt = VT_R4; v.fltVal = q;
				bag->Write(1, &opt, &v);
				VariantClear(&v);
			};
	}

	DirectX::WICCodecs codec = CodecFromFormat(fmt);
	EnsureCOMForWIC();
	return SUCCEEDED(DirectX::SaveToWICFile(
		*toSave,
		DirectX::WIC_FLAGS_NONE,
		DirectX::GetWICCodec(codec),
		path.c_str(),
		nullptr,
		setProps));
#else
	(void)path; (void)format; (void)bGenerateMips; (void)mipLevels; return false;
#endif
}


bool Image::SaveToCubeMapDDS(const std::filesystem::path& path, const Image faces[6], bool bGenerateMips, size_t mipLevels)
{
#ifdef DXTEX
	ASSERT(faces, "Image::SaveToCubeMapDDS - faces is null");
	if (!faces)
	{
		return false;
	}

	const ImageState* st0 = reinterpret_cast<const ImageState*>(faces[0].m_pData);
	ASSERT(st0 && faces[0].IsValid(), "Image::SaveToCubeMapDDS - face[0] invalid");
	if (!st0 || !faces[0].IsValid())
	{
		return false;
	}

	const uint w = static_cast<uint>(st0->scratch.GetMetadata().width);
	const uint h = static_cast<uint>(st0->scratch.GetMetadata().height);
	auto fmt = st0->scratch.GetMetadata().format;

	DirectX::TexMetadata md = {};
	md.width = w; 
	md.height = h; 
	md.depth = 1;
	md.arraySize = 6;
	md.mipLevels = 1;
	md.format = fmt; 
	md.dimension = DirectX::TEX_DIMENSION_TEXTURE2D;
	md.miscFlags |= DirectX::TEX_MISC_TEXTURECUBE;

	DirectX::ScratchImage cube;
	if (FAILED(cube.Initialize(md)))
	{
		return false;
	}

	for (size_t i = 0; i < 6; ++i)
	{
		const auto* sti = reinterpret_cast<const ImageState*>(faces[i].m_pData);
		ASSERT(sti && faces[i].IsValid(), "Image::SaveToCubeMapDDS - some face invalid");
		if (!sti || !faces[i].IsValid())
		{
			return false;
		}

		const DirectX::Image* imDst = cube.GetImage(0, i, 0);
		const auto* imSrc = sti->scratch.GetImage(0, 0, 0);
		if (!imSrc || imSrc->width != imDst->width || imSrc->height != imDst->height || sti->scratch.GetMetadata().format != fmt)
		{
			return false;
		}

		for (size_t y = 0; y < imDst->height; ++y)
		{
			std::memcpy(imDst->pixels + y * imDst->rowPitch, imSrc->pixels + y * imSrc->rowPitch, imDst->rowPitch);
		}
	}

	const DirectX::Image* images = cube.GetImages(); 
	size_t n = cube.GetImageCount(); 
	DirectX::TexMetadata mdc = cube.GetMetadata();

	DirectX::ScratchImage mipped;
	if (bGenerateMips)
	{
		EnsureCOMForWIC();
		if (FAILED(DirectX::GenerateMipMaps(images, n, mdc, DirectX::TEX_FILTER_DEFAULT, mipLevels, mipped)))
		{
			return false;
		}
		images = mipped.GetImages(); 
		n = mipped.GetImageCount();
		mdc = mipped.GetMetadata();
		mdc.miscFlags |= DirectX::TEX_MISC_TEXTURECUBE;
	}

	return SUCCEEDED(DirectX::SaveToDDSFile(images, n, mdc, DirectX::DDS_FLAGS_NONE, path.c_str()));
#else
	(void)path; (void)faces; (void)bGenerateMips; (void)mipLevels; return false;
#endif
}

// -------------------------------------
// Utilities
// -------------------------------------
void Image::Reset()
{
#ifdef DXTEX
	if (m_pData) 
	{
		ImageState* st = reinterpret_cast<ImageState*>(m_pData);
		st->scratch.Release();
		m_pData = nullptr;
	}
#endif
	m_Header = GeneralHeader{};
}
