#pragma once
#include <vector>
#include "Common/Common.h"

enum IMAGE_FORMAT
{
	IMAGE_FORMAT_UNKNOWN = 0,
	IMAGE_FORMAT_RGBA8,
	IMAGE_FORMAT_RGB8,
	IMAGE_FORMAT_R8,
	IMAGE_FORMAT_BC1, // DXT1
	IMAGE_FORMAT_BC3, // DXT5
	IMAGE_FORMAT_BC4, // ATI1
	IMAGE_FORMAT_BC5, // ATI2
	IMAGE_FORMAT_BC6H,
	IMAGE_FORMAT_BC7,
};

struct Image
{
	uint Width = 0;
	uint Height = 0;
	uint Channels = 0;
	std::vector<RGBA> Data; // RGBA format
};

void FillImageRGBA(Image& img, int w, int h, const unsigned char* rgba8);
bool LoadImageFromFile(const std::filesystem::path& path, Image* outImage);
bool SaveImageToFile(const std::filesystem::path& path, const Image& img, IMAGE_FORMAT format = IMAGE_FORMAT_RGBA8);

struct aiScene;
struct aiMaterial;
enum aiTextureType;

bool LoadExternalTexture(
	const std::filesystem::path& modelDir,
	const std::string& relPath,
	Image& outImg);
bool LoadEmbeddedTexture(
	const aiScene* scene,
	const std::string& starPath,
	Image& outImg);

bool TryLoadMaterialTexture(
	const aiScene* scene,
	const aiMaterial* mat,
	aiTextureType type,
	const std::filesystem::path& modelDir,
	Image& outImg);
bool TryLoadNormalLike(
	const aiScene* scene,
	const aiMaterial* mat,
	const std::filesystem::path& modelDir,
	Image& outImg);
bool TryLoadMetallic(
	const aiScene* scene,
	const aiMaterial* mat,
	const std::filesystem::path& modelDir,
	Image& outImg);
bool TryLoadRoughness(
	const aiScene* scene,
	const aiMaterial* mat,
	const std::filesystem::path& modelDir,
	Image& outImg);