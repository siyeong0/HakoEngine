// Image.h — Revamped, engine-friendly image container
// - Single owner of CPU-side pixels (DirectXTex ScratchImage inside)
// - Loads/Saves common formats (PNG, JPG, TGA, BMP, HDR) and DDS
// - Optional runtime BCn compression, mipmap generation, format convert
// - Low-level accessors for GPU upload: data pointer, row/slice pitch per subresource
// - Minimal globals; all functionality lives on Image
// - Requires: DirectXTex (vcpkg: directxtex). Optional: stb (header-only fallback for non-Windows WIC)
//
// Notes:
// * On Windows, non-DDS formats use WIC via DirectXTex. On non-Windows, these fall back to stb_image/write.
// * DDS handling (parse/save/compress/mips) uses DirectXTex on all platforms.
// * For EXR support, consider adding "directxtex-exr" or OpenEXR via vcpkg (see comments below).

#pragma once
#include <algorithm>
#include <variant>

#ifdef _WIN32
#define DXTEX
#else

#endif 

#ifdef DXTEX
#include <dxgiformat.h>
#endif

#include "Common/Common.h"

struct Color;
struct RGBA;

class Image
{
public:
	// ----------------------------
	// Public enums & POD headers
	// ----------------------------

	// Image file format
	enum class EImageFormat { Auto, DDS, PNG, JPEG, BMP, TIFF, GIF, WMP, TGA, HDR };

	// -----------------------------------------------------------------------------
	// Convenient FORMAT aliases (DXGI-compatible numeric enum)
	// Keep names short & consistent across 8 / 16 / 32-bit families
	// -----------------------------------------------------------------------------

	// ----------------------------
	// Header variants
	// ----------------------------

	struct GeneralHeader
	{
		uint Width = 0;
		uint Height = 0;
		uint Channels = 0; // 1/2/3/4 (informational)
		TEXFORMAT Format = TEXFORMAT_UNKNOWN; // prefer DXGI-like format id
	};

	struct DDSHeader
	{
		uint Width = 0;
		uint Height = 0;
		uint MipLevels = 1;   // at least 1
		uint ArraySize = 1;   // 1 for 2D texture, >1 for arrays/cubemaps
		bool bCubeMap = false;
		TEXFORMAT Format = TEXFORMAT_UNKNOWN; // BCn or uncompressed
	};

	using Header = std::variant<GeneralHeader, DDSHeader>;

public:
	// ----------------------------
	// Construction & lifetime
	// ----------------------------
	Image();
	Image(Image&&) noexcept;
	Image& operator=(Image&&) noexcept;
	Image(const Image&) = delete;
	Image& operator=(const Image&) = delete;
	~Image();

	// ----------------------------
	// Creation
	// ----------------------------
	bool Load(const std::filesystem::path& path);
	bool LoadFromMemory(const void* data, size_t sizeBytes, std::string_view hintExtension = {});
	bool LoadFromMemory(const RGBA* data, size_t sizeBytes, std::string_view hintExtension = {});
	bool LoadFromMemory(const Color* data, size_t sizeBytes, std::string_view hintExtension = {});

	static bool Load(const std::filesystem::path& path, Image* out);
	static bool LoadFromMemory(const void* data, size_t sizeBytes, Image* out, std::string_view hintExtension = {});

	// Create a blank RGBA8 image with optional mip chain (all black)
	static Image CreateBlank(uint w, uint h, TEXFORMAT fmt = TEXFORMAT_RGBA8_UNORM, uint mips = 1, uint arraySize = 1, bool bCubeMap = false);

	// Create a solid-color image with optional mip chain
	static Image CreateSolidColor(uint w, uint h, const uint8_t rgba[4], TEXFORMAT fmt = TEXFORMAT_RGBA8_UNORM, uint mips = 1, uint arraySize = 1, bool bCubeMap = false);
	static Image CreateSolidColor(uint w, uint h, const RGBA& rgba, TEXFORMAT fmt = TEXFORMAT_RGBA8_UNORM, uint mips = 1, uint arraySize = 1, bool bCubeMap = false);
	static Image CreateSolidColor(uint w, uint h, const Color& color, TEXFORMAT fmt = TEXFORMAT_RGBA8_UNORM, uint mips = 1, uint arraySize = 1, bool bCubeMap = false);

	// ----------------------------
	// Introspection (metadata)
	// ----------------------------
	bool IsValid() const;
	bool IsDDS() const;
	Header GetHeader() const;
	TEXFORMAT GetFormat() const;
	uint GetWidth() const;
	uint GetHeight() const;
	uint GetChannels() const;
	uint GetMipCount() const;
	uint GetArraySize() const;
	bool IsCubeMap() const;

	// ----------------------------
	// Subresource access (for GPU upload)
	// item: array slice; mip: mip level; slice: depth slice (for 3D)
	// Returns nullptr if out of range
	// ----------------------------
	const void* GetDataPtr(uint item = 0, uint mip = 0, uint slice = 0) const;
	size_t GetRowPitch(uint item = 0, uint mip = 0, uint slice = 0) const;
	size_t GetSlicePitch(uint item = 0, uint mip = 0, uint slice = 0) const;

	// Flatten top mip to tightly packed RGBA8 buffer (converts if needed)
	std::vector<uint8_t> CopyTopMipAsRGBA8() const;

	// ----------------------------
	// Editing / processing (in-place)
	// ----------------------------
	bool Convert(TEXFORMAT newFmt, bool bUseSRGB = false);
	bool Resize(uint width, uint height, bool bUseSRGB = false);
	bool GenerateMips(
		uint desiredMipCount = 0,   // 0 = full chain
		uint filter = 0,            // DirectX::TEX_FILTER_DEFAULT (set in .cpp)
		bool bUseSRGB = false);     // gamma-correct filtering
	bool FlipY();
	bool Decompress(); // if BCn → uncompressed (RGBA8 by default)
	bool Compress(
		TEXFORMAT target = TEXFORMAT_UNKNOWN, // e.g., BC1, BC3, BC5, BC7
		unsigned long flags = 0,            // DirectX::TEX_COMPRESS_DEFAULT (set in .cpp)
		bool bUseSRGB = false);				// uncompressed → BCn

	// ----------------------------
	// Save
	// ----------------------------
	bool Save(const std::filesystem::path& path, EImageFormat format = EImageFormat::Auto, bool bGenerateMips = false, size_t mipLevels = 0) const;
	static bool SaveToCubeMapDDS(const std::filesystem::path& path, const Image faces[6], bool bGenerateMips = false, size_t mipLevels = 0);

	// ----------------------------
	// Utilities
	// ----------------------------
	void Reset();

	// ----------------------------
	// JPEG quality for saving (0.0f - 1.0f)
	// ----------------------------
	static void SetJPEGQuality(float quality) { m_sJPEGQuality = std::clamp(quality, 0.0f, 1.0f); }
	static float GetJPEGQuality() { return m_sJPEGQuality; }
private:
	Header m_Header;
	uint8_t* m_pData = nullptr;

	inline static float m_sJPEGQuality = 0.92f;
};
