#pragma once
#include <vector>
#include "Common/Common.h"

struct Image
{
	uint Width = 0;
	uint Height = 0;
	uint Channels = 0;
	std::vector<RGBA> Data; // RGBA format
};

void FillImageRGBA(Image& img, int w, int h, const unsigned char* rgba8);
bool LoadImageFromFile(const std::filesystem::path& path, Image* outImage);

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