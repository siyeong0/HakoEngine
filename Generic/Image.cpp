#include "pch.h"
#pragma warning(push)
#pragma warning(disable : 6262)
#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb_image.h>
#include <stb_image_write.h>
#pragma warning(pop)

#include <assimp/Importer.hpp>
#include <assimp/scene.h>
#include <assimp/postprocess.h>

#include "Image.h"

Image Image::LoadFromFile(const char* path)
{
	ASSERT(path, "Image path is null");
	return LoadFromFile(std::filesystem::path(path));
}

Image Image::LoadFromFile(const wchar_t* path)
{
	ASSERT(path, "Image wide path is null");
	return Image::LoadFromFile(std::filesystem::path(path));
}

Image Image::LoadFromFile(const std::filesystem::path& path)
{
	ASSERT(!path.empty(), "Image path is empty");
	Image img;

	std::error_code ec;
	if (!std::filesystem::exists(path, ec))
	{
		std::cout << "[Image] Texture not found: " << path << "\n";
		return img;
	}

	std::ifstream ifs(path, std::ios::binary);
	if (!ifs)
	{
		std::cout << "[Image] Failed to open: " << path << "\n";
		return img;
	}

	ifs.seekg(0, std::ios::end);
	std::streamoff len = ifs.tellg();
	if (len <= 0)
	{
		std::cout << "[Image] Empty file: " << path << "\n";
		return img;
	}
	ifs.seekg(0, std::ios::beg);

	std::vector<unsigned char> buffer(static_cast<size_t>(len));
	if (!ifs.read(reinterpret_cast<char*>(buffer.data()), len))
	{
		std::cout << "[Image] Failed to read: " << path << "\n";
		return img;
	}

	return Image::LoadFromMemory(buffer.data(), buffer.size());
}

Image Image::LoadFromMemory(const unsigned char* fileData, size_t dataSize)
{
	ASSERT(fileData && dataSize > 0, "Invalid image memory");
	Image img;

	if (dataSize > static_cast<size_t>(INT_MAX))
	{
		std::cout << "[Image] Data too large for stb_image (" << dataSize << " bytes)\n";
		return img;
	}

	int w = 0, h = 0, comp = 0;
	unsigned char* pixels = stbi_load_from_memory(
		fileData,
		static_cast<int>(dataSize),
		&w, &h, &comp,
		4 // force RGBA8
	);

	if (!pixels)
	{
		std::cout << "[Image] stbi_load_from_memory failed: "
			<< (stbi_failure_reason() ? stbi_failure_reason() : "unknown") << "\n";
		return img;
	}

	FillImageRGBA(&img, static_cast<uint>(w), static_cast<uint>(h), pixels);
	stbi_image_free(pixels);
	return img;
}

void FillImageRGBA(Image* outImg, uint w, uint h, const unsigned char* rgba8)
{
	outImg->Width = w;
	outImg->Height = h;
	outImg->Channels = 4;
	outImg->Data.resize(size_t(w) * size_t(h));
	for (size_t i = 0; i < outImg->Data.size(); ++i)
	{
		const unsigned char* p = rgba8 + i * 4;
		std::memcpy((void*)(&outImg->Data[i]), p, 4);
	}
}

void FillImageRGBA(Image* outImg, uint w, uint h, const RGBA* rgba)
{
	FillImageRGBA(outImg, w, h, reinterpret_cast<const unsigned char*>(rgba));
}

Image CreateSolidColorImageRGBA(uint w, uint h, const RGBA& color)
{
	Image img;
	img.Width = w;
	img.Height = h;
	img.Channels = 4;
	img.Data.resize(size_t(w) * size_t(h), color);

	return img;
}

Image CreateCheckerboardImageRGBA(uint w, uint h, const RGBA& color1, const RGBA& color2, int checkerSize)
{
	checkerSize = (checkerSize <= 0) ? std::min(w, h) / 8 : checkerSize;

	Image img;
	img.Width = w;
	img.Height = h;
	img.Channels = 4;
	img.Data.resize(size_t(w) * size_t(h));
	for (uint y = 0; y < h; ++y)
	{
		for (uint x = 0; x < w; ++x)
		{
			bool bUseColor1 = ((x / checkerSize) % 2) == ((y / checkerSize) % 2);
			img.Data[size_t(y) * size_t(w) + size_t(x)] = bUseColor1 ? color1 : color2;
		}
	}
	return img;
}

// -----------------------------
// Helpers: Image loaders
// -----------------------------

bool LoadImageFromFile(const std::filesystem::path& path, Image* outImage)
{
	ASSERT(outImage, "Image output pointer is null");
	int w = 0, h = 0, comp = 0;
	unsigned char* pixels = stbi_load(path.string().c_str(), &w, &h, &comp, 4);
	if (!pixels)
	{
		std::cout << "[Image] Texture not found: " << path << "\n";
		return false;
	}
	FillImageRGBA(outImage, static_cast<uint>(w), static_cast<uint>(h), pixels);
	stbi_image_free(pixels);
	return true;
}

bool SaveImageToFile(const std::filesystem::path& path, const Image& img, IMAGE_FORMAT format)
{
	if (img.Data.empty() || img.Width == 0 || img.Height == 0)
		return false;

	int comp = 4;
	const void* dataPtr = img.Data.data();

	switch (format)
	{
	case IMAGE_FORMAT_R8:
		comp = 1;
		break;
	case IMAGE_FORMAT_RGB8:
		comp = 3;
		break;
	case IMAGE_FORMAT_RGBA8:
		comp = 4;
		break;

	case IMAGE_FORMAT_BC1:
	case IMAGE_FORMAT_BC3:
	case IMAGE_FORMAT_BC4:
	case IMAGE_FORMAT_BC5:
	case IMAGE_FORMAT_BC6H:
	case IMAGE_FORMAT_BC7:
		// TODO: use DDSTexutreLoader, etc.
		std::cout << "[SaveImageToFile] WARNING: BC-compressed formats (BC1~BC7) "
			"cannot be written as PNG. Saving as RGBA8 fallback.\n";
		comp = 4;
		break;

	default:
		std::cout << "[SaveImageToFile] Unknown image format, defaulting to RGBA8.\n";
		comp = 4;
		break;
	}

	int stride = int(img.Width) * comp;

	int res = stbi_write_png(
		path.string().c_str(),
		int(img.Width),
		int(img.Height),
		comp,
		dataPtr,
		stride);

	if (res == 0)
	{
		std::cerr << "[SaveImageToFile] Failed to write image: " << path << "\n";
		return false;
	}

	return true;
}

#include "lodepng.h" // https://github.com/lvandeve/lodepng (single header)

bool SaveGray16PNG(const std::filesystem::path& path,
	const uint16_t* gray16,
	uint32_t width, uint32_t height)
{
	if (!gray16 || width == 0 || height == 0) return false;

	// PNG는 16bit를 big-endian(상위 바이트 먼저)로 저장해야 함
	const size_t N = size_t(width) * height;
	std::vector<unsigned char> raw(2 * N);
	for (size_t i = 0; i < N; ++i) {
		uint16_t v = gray16[i];
		raw[2 * i + 0] = (unsigned char)((v >> 8) & 0xFF); // hi
		raw[2 * i + 1] = (unsigned char)(v & 0xFF);        // lo
	}

	lodepng::State state;
	state.info_raw.colortype = LCT_GREY;   // 1채널
	state.info_raw.bitdepth = 16;         // 16비트
	state.info_png.color.colortype = LCT_GREY;
	state.info_png.color.bitdepth = 16;
	state.encoder.auto_convert = 0;        // 우리가 지정한 포맷 그대로 사용

	std::vector<unsigned char> png;
	unsigned err = lodepng::encode(png, raw, width, height, state);
	if (err) {
		std::cerr << "[SaveGray16PNG] lodepng error " << err
			<< ": " << lodepng_error_text(err) << "\n";
		return false;
	}

	// 파일로 저장
	err = lodepng::save_file(png, path.string());
	if (err) {
		std::cerr << "[SaveGray16PNG] save_file error " << err
			<< ": " << lodepng_error_text(err) << "\n";
		return false;
	}
	return true;
}
