#define _CRT_SECURE_NO_WARNINGS
#include <iostream>
#include <filesystem>

#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb_image.h>
#include <stb_image_write.h>

#include <assimp/Importer.hpp>
#include <assimp/scene.h>
#include <assimp/postprocess.h>

#include "Image.h"

// -----------------------------
// Helpers: Image loaders
// -----------------------------

void FillImageRGBA(Image& img, int w, int h, const unsigned char* rgba8)
{
	img.Width = w;
	img.Height = h;
	img.Channels = 4;
	img.Data.resize(size_t(w) * size_t(h));
	for (size_t i = 0; i < img.Data.size(); ++i)
	{
		const unsigned char* p = rgba8 + i * 4;
		std::memcpy(img.Data[i].ColorData, p, 4);
	}
}

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
	FillImageRGBA(*outImage, w, h, pixels);
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

// Load external file (relative to model dir) via stb_image → RGBA8
bool LoadExternalTexture(
	const std::filesystem::path& modelDir,
	const std::string& relPath,
	Image& outImg)
{
	if (relPath.empty()) return false;

	// Assimp material path can be absolute/relative; try relative first
	std::filesystem::path full = modelDir / relPath;
	if (!std::filesystem::exists(full))
	{
		// Fallback: as is (maybe absolute)
		full = relPath;
		if (!std::filesystem::exists(full))
		{
			std::fprintf(stderr, "[StaticMesh] Texture not found: %s\n", relPath.c_str());
			return false;
		}
	}

	int w = 0, h = 0, comp = 0;
	unsigned char* pixels = stbi_load(full.string().c_str(), &w, &h, &comp, 4);
	if (!pixels)
	{
		std::fprintf(stderr, "[StaticMesh] stbi_load failed: %s\n", full.string().c_str());
		return false;
	}

	FillImageRGBA(outImg, w, h, pixels);
	stbi_image_free(pixels);

	return true;
}

// Load embedded texture (*N) from aiScene → RGBA8/float
bool LoadEmbeddedTexture(
	const aiScene* scene,
	const std::string& starPath,
	Image& outImg)
{
	// "*0" → 0
	int texIndex = 0;
	try { texIndex = std::stoi(starPath.substr(1)); }
	catch (...) { return false; }

	if (texIndex < 0 || texIndex >= int(scene->mNumTextures)) return false;
	const aiTexture* tex = scene->mTextures[texIndex];
	if (!tex) return false;

	if (tex->mHeight == 0)
	{
		// Compressed data (PNG/JPG/…)
		int w = 0, h = 0, comp = 0;
		const unsigned char* src = reinterpret_cast<const unsigned char*>(tex->pcData);
		unsigned char* pixels = stbi_load_from_memory(src, tex->mWidth, &w, &h, &comp, 4);
		if (!pixels)
		{
			std::fprintf(stderr, "[StaticMesh] stbi_load_from_memory failed for embedded texture %s\n", starPath.c_str());
			return false;
		}
		FillImageRGBA(outImg, w, h, pixels);
		stbi_image_free(pixels);
		return true;
	}
	else
	{
		// Uncompressed (aiTexel RGBA8888)
		int w = int(tex->mWidth);
		int h = int(tex->mHeight);
		std::vector<unsigned char> rgba; rgba.resize(size_t(w) * size_t(h) * 4);
		const aiTexel* src = tex->pcData;
		for (int i = 0; i < w * h; ++i)
		{
			rgba[i * 4 + 0] = src[i].r;
			rgba[i * 4 + 1] = src[i].g;
			rgba[i * 4 + 2] = src[i].b;
			rgba[i * 4 + 3] = src[i].a;
		}
		FillImageRGBA(outImg, w, h, rgba.data());
		return true;
	}
}

bool TryLoadMaterialTexture(
	const aiScene* scene,
	const aiMaterial* mat,
	aiTextureType type,
	const std::filesystem::path& modelDir,
	Image& outImg)
{
	if (!mat) return false;

	aiString path;
	if (mat->GetTexture(type, 0, &path) != AI_SUCCESS) return false;

	std::string p = path.C_Str();
	if (p.empty()) return false;

	// Embedded?
	if (p[0] == '*')
	{
		return LoadEmbeddedTexture(scene, p, outImg);
	}
	// External
	return LoadExternalTexture(modelDir, p, outImg);
}

// Many assets put normal map in aiTextureType_HEIGHT. Try NORMALS first, then HEIGHT.
bool TryLoadNormalLike(
	const aiScene* scene,
	const aiMaterial* mat,
	const std::filesystem::path& modelDir,
	Image& outImg)
{
	if (TryLoadMaterialTexture(scene, mat, aiTextureType_NORMALS, modelDir, outImg)) return true;
	if (TryLoadMaterialTexture(scene, mat, aiTextureType_HEIGHT, modelDir, outImg)) return true;
	return false;
}

// Metallic & Roughness can be stored in aiTextureType_METALNESS / aiTextureType_DIFFUSE_ROUGHNESS.
// Some exporters put PBR maps under aiTextureType_UNKNOWN too; you could add fallback if needed.
bool TryLoadMetallic(
	const aiScene* scene,
	const aiMaterial* mat,
	const std::filesystem::path& modelDir,
	Image& outImg)
{
	if (TryLoadMaterialTexture(scene, mat, aiTextureType_METALNESS, modelDir, outImg)) return true;
	// Optional: UNKNOWN fallback or combined ORM parsing could go here
	return false;
}

bool TryLoadRoughness(
	const aiScene* scene,
	const aiMaterial* mat,
	const std::filesystem::path& modelDir,
	Image& outImg)
{
	if (TryLoadMaterialTexture(scene, mat, aiTextureType_DIFFUSE_ROUGHNESS, modelDir, outImg)) return true;
	return false;
}
