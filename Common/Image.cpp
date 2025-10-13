#include <filesystem>
#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>
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
		std::fprintf(stderr, "[Image] stbi_load failed: %s\n", path.string().c_str());
		return false;
	}
	FillImageRGBA(*outImage, w, h, pixels);
	stbi_image_free(pixels);
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
