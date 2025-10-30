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

    // Fully DXGI-compatible numeric values, with "DXGI_" removed from names.
    enum FORMAT : uint32_t
    {
        FORMAT_UNKNOWN = 0,
        FORMAT_R32G32B32A32_TYPELESS = 1,
        FORMAT_R32G32B32A32_FLOAT = 2,
        FORMAT_R32G32B32A32_UINT = 3,
        FORMAT_R32G32B32A32_SINT = 4,
        FORMAT_R32G32B32_TYPELESS = 5,
        FORMAT_R32G32B32_FLOAT = 6,
        FORMAT_R32G32B32_UINT = 7,
        FORMAT_R32G32B32_SINT = 8,
        FORMAT_R16G16B16A16_TYPELESS = 9,
        FORMAT_R16G16B16A16_FLOAT = 10,
        FORMAT_R16G16B16A16_UNORM = 11,
        FORMAT_R16G16B16A16_UINT = 12,
        FORMAT_R16G16B16A16_SNORM = 13,
        FORMAT_R16G16B16A16_SINT = 14,
        FORMAT_R32G32_TYPELESS = 15,
        FORMAT_R32G32_FLOAT = 16,
        FORMAT_R32G32_UINT = 17,
        FORMAT_R32G32_SINT = 18,
        FORMAT_R32G8X24_TYPELESS = 19,
        FORMAT_D32_FLOAT_S8X24_UINT = 20,
        FORMAT_R32_FLOAT_X8X24_TYPELESS = 21,
        FORMAT_X32_TYPELESS_G8X24_UINT = 22,
        FORMAT_R10G10B10A2_TYPELESS = 23,
        FORMAT_R10G10B10A2_UNORM = 24,
        FORMAT_R10G10B10A2_UINT = 25,
        FORMAT_R11G11B10_FLOAT = 26,
        FORMAT_R8G8B8A8_TYPELESS = 27,
        FORMAT_R8G8B8A8_UNORM = 28,
        FORMAT_R8G8B8A8_UNORM_SRGB = 29,
        FORMAT_R8G8B8A8_UINT = 30,
        FORMAT_R8G8B8A8_SNORM = 31,
        FORMAT_R8G8B8A8_SINT = 32,
        FORMAT_R16G16_TYPELESS = 33,
        FORMAT_R16G16_FLOAT = 34,
        FORMAT_R16G16_UNORM = 35,
        FORMAT_R16G16_UINT = 36,
        FORMAT_R16G16_SNORM = 37,
        FORMAT_R16G16_SINT = 38,
        FORMAT_R32_TYPELESS = 39,
        FORMAT_D32_FLOAT = 40,
        FORMAT_R32_FLOAT = 41,
        FORMAT_R32_UINT = 42,
        FORMAT_R32_SINT = 43,
        FORMAT_R24G8_TYPELESS = 44,
        FORMAT_D24_UNORM_S8_UINT = 45,
        FORMAT_R24_UNORM_X8_TYPELESS = 46,
        FORMAT_X24_TYPELESS_G8_UINT = 47,
        FORMAT_R8G8_TYPELESS = 48,
        FORMAT_R8G8_UNORM = 49,
        FORMAT_R8G8_UINT = 50,
        FORMAT_R8G8_SNORM = 51,
        FORMAT_R8G8_SINT = 52,
        FORMAT_R16_TYPELESS = 53,
        FORMAT_R16_FLOAT = 54,
        FORMAT_D16_UNORM = 55,
        FORMAT_R16_UNORM = 56,
        FORMAT_R16_UINT = 57,
        FORMAT_R16_SNORM = 58,
        FORMAT_R16_SINT = 59,
        FORMAT_R8_TYPELESS = 60,
        FORMAT_R8_UNORM = 61,
        FORMAT_R8_UINT = 62,
        FORMAT_R8_SNORM = 63,
        FORMAT_R8_SINT = 64,
        FORMAT_A8_UNORM = 65,
        FORMAT_R1_UNORM = 66,
        FORMAT_R9G9B9E5_SHAREDEXP = 67,
        FORMAT_R8G8_B8G8_UNORM = 68,
        FORMAT_G8R8_G8B8_UNORM = 69,
        FORMAT_BC1_TYPELESS = 70,
        FORMAT_BC1_UNORM = 71,
        FORMAT_BC1_UNORM_SRGB = 72,
        FORMAT_BC2_TYPELESS = 73,
        FORMAT_BC2_UNORM = 74,
        FORMAT_BC2_UNORM_SRGB = 75,
        FORMAT_BC3_TYPELESS = 76,
        FORMAT_BC3_UNORM = 77,
        FORMAT_BC3_UNORM_SRGB = 78,
        FORMAT_BC4_TYPELESS = 79,
        FORMAT_BC4_UNORM = 80,
        FORMAT_BC4_SNORM = 81,
        FORMAT_BC5_TYPELESS = 82,
        FORMAT_BC5_UNORM = 83,
        FORMAT_BC5_SNORM = 84,
        FORMAT_B5G6R5_UNORM = 85,
        FORMAT_B5G5R5A1_UNORM = 86,
        FORMAT_B8G8R8A8_UNORM = 87,
        FORMAT_B8G8R8X8_UNORM = 88,
        FORMAT_R10G10B10_XR_BIAS_A2_UNORM = 89,
        FORMAT_B8G8R8A8_TYPELESS = 90,
        FORMAT_B8G8R8A8_UNORM_SRGB = 91,
        FORMAT_B8G8R8X8_TYPELESS = 92,
        FORMAT_B8G8R8X8_UNORM_SRGB = 93,
        FORMAT_BC6H_TYPELESS = 94,
        FORMAT_BC6H_UF16 = 95,
        FORMAT_BC6H_SF16 = 96,
        FORMAT_BC7_TYPELESS = 97,
        FORMAT_BC7_UNORM = 98,
        FORMAT_BC7_UNORM_SRGB = 99,
        FORMAT_AYUV = 100,
        FORMAT_Y410 = 101,
        FORMAT_Y416 = 102,
        FORMAT_NV12 = 103,
        FORMAT_P010 = 104,
        FORMAT_P016 = 105,
        FORMAT_420_OPAQUE = 106,
        FORMAT_YUY2 = 107,
        FORMAT_Y210 = 108,
        FORMAT_Y216 = 109,
        FORMAT_NV11 = 110,
        FORMAT_AI44 = 111,
        FORMAT_IA44 = 112,
        FORMAT_P8 = 113,
        FORMAT_A8P8 = 114,
        FORMAT_B4G4R4A4_UNORM = 115,

        // 116–129 are intentionally unused in DXGI

        FORMAT_P208 = 130,
        FORMAT_V208 = 131,
        FORMAT_V408 = 132,

        // 133–188 are intentionally unused in DXGI

        FORMAT_SAMPLER_FEEDBACK_MIN_MIP_OPAQUE = 189,
        FORMAT_SAMPLER_FEEDBACK_MIP_REGION_USED_OPAQUE = 190,

        FORMAT_A4B4G4R4_UNORM = 191,

        FORMAT_FORCE_UINT = 0xffffffffu
    };

    // -----------------------------------------------------------------------------
    // Convenient FORMAT aliases (DXGI-compatible numeric enum)
    // Keep names short & consistent across 8 / 16 / 32-bit families
    // -----------------------------------------------------------------------------

    // 8-bit UNORM / sRGB
    static constexpr FORMAT FORMAT_RGBA8_UNORM = FORMAT_R8G8B8A8_UNORM;
    static constexpr FORMAT FORMAT_RGBA8_UNORM_SRGB = FORMAT_R8G8B8A8_UNORM_SRGB;
    static constexpr FORMAT FORMAT_BGRA8_UNORM = FORMAT_B8G8R8A8_UNORM;
    static constexpr FORMAT FORMAT_BGRA8_UNORM_SRGB = FORMAT_B8G8R8A8_UNORM_SRGB;

    // 16-bit
    static constexpr FORMAT FORMAT_RG16_UNORM = FORMAT_R16G16_UNORM;
    static constexpr FORMAT FORMAT_RG16_FLOAT = FORMAT_R16G16_FLOAT;
    static constexpr FORMAT FORMAT_RGBA16_UNORM = FORMAT_R16G16B16A16_UNORM;
    static constexpr FORMAT FORMAT_RGBA16_FLOAT = FORMAT_R16G16B16A16_FLOAT;

    // 32-bit (float/uint/sint)
    static constexpr FORMAT FORMAT_RG32_FLOAT = FORMAT_R32G32_FLOAT;
    static constexpr FORMAT FORMAT_RG32_UINT = FORMAT_R32G32_UINT;
    static constexpr FORMAT FORMAT_RG32_SINT = FORMAT_R32G32_SINT;
    static constexpr FORMAT FORMAT_RGBA32_FLOAT = FORMAT_R32G32B32A32_FLOAT;
    static constexpr FORMAT FORMAT_RGBA32_UINT = FORMAT_R32G32B32A32_UINT;
    static constexpr FORMAT FORMAT_RGBA32_SINT = FORMAT_R32G32B32A32_SINT;

    // Single-channel (common)
    static constexpr FORMAT FORMAT_R8_UNORM_ALIAS = FORMAT_R8_UNORM;   // (이름 충돌 피하려 _ALIAS)
    static constexpr FORMAT FORMAT_R16_UNORM_ALIAS = FORMAT_R16_UNORM;  // "
    static constexpr FORMAT FORMAT_R16_FLOAT_ALIAS = FORMAT_R16_FLOAT;  // "
    static constexpr FORMAT FORMAT_R32_FLOAT_ALIAS = FORMAT_R32_FLOAT;  // "

    // Depth/Stencil shorthands
    static constexpr FORMAT FORMAT_D16 = FORMAT_D16_UNORM;
    static constexpr FORMAT FORMAT_D24S8 = FORMAT_D24_UNORM_S8_UINT;
    static constexpr FORMAT FORMAT_D32F = FORMAT_D32_FLOAT;
    static constexpr FORMAT FORMAT_D32FS8X24 = FORMAT_D32_FLOAT_S8X24_UINT;

	// ----------------------------
	// Header variants
	// ----------------------------

	struct GeneralHeader
	{
		uint Width = 0;
		uint Height = 0;
		uint Channels = 0; // 1/2/3/4 (informational)
		FORMAT Format = FORMAT_UNKNOWN; // prefer DXGI-like format id
	};

	struct DDSHeader
	{
		uint Width = 0;
		uint Height = 0;
		uint MipLevels = 1;   // at least 1
		uint ArraySize = 1;   // 1 for 2D texture, >1 for arrays/cubemaps
		bool bCubeMap = false;
		FORMAT Format = FORMAT_UNKNOWN; // BCn or uncompressed
	};

	using Header = std::variant<GeneralHeader, DDSHeader>;

public:
	// ----------------------------
	// Construction & lifetime
	// ----------------------------
	Image();
	Image(const Image&);
	Image(Image&&) noexcept;
	Image& operator=(const Image&);
	Image& operator=(Image&&) noexcept;
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
	static Image CreateBlank(uint w, uint h, FORMAT fmt = FORMAT_RGBA8_UNORM, uint mips = 1, uint arraySize = 1, bool bCubeMap = false);

	// Create a solid-color image with optional mip chain
	static Image CreateSolidColor(uint w, uint h, const uint8_t rgba[4], FORMAT fmt = FORMAT_RGBA8_UNORM, uint mips = 1, uint arraySize = 1, bool bCubeMap = false);
	static Image CreateSolidColor(uint w, uint h, const RGBA& rgba, FORMAT fmt = FORMAT_RGBA8_UNORM, uint mips = 1, uint arraySize = 1, bool bCubeMap = false);
	static Image CreateSolidColor(uint w, uint h, const Color& color, FORMAT fmt = FORMAT_RGBA8_UNORM, uint mips = 1, uint arraySize = 1, bool bCubeMap = false);

	// ----------------------------
	// Introspection (metadata)
	// ----------------------------
	bool IsValid() const;
	bool IsDDS() const;
	Header GetHeader() const;
	FORMAT GetFormat() const;
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
	bool Convert(FORMAT newFmt, bool bUseSRGB = false);
	bool Resize(uint width, uint height, bool bUseSRGB = false);
	bool GenerateMips(
		uint desiredMipCount = 0,   // 0 = full chain
		uint filter = 0,            // DirectX::TEX_FILTER_DEFAULT (set in .cpp)
		bool bUseSRGB = false);     // gamma-correct filtering
	bool FlipY();
	bool Decompress(); // if BCn → uncompressed (RGBA8 by default)
	bool Compress(
		FORMAT target = FORMAT_UNKNOWN, // e.g., BC1, BC3, BC5, BC7
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
