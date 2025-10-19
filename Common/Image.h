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

	bool IsValid() const { return Width > 0 && Height > 0 && !Data.empty(); }
};

void FillImageRGBA(Image* outImg, uint w, uint h, const unsigned char* rgba8);
void FillImageRGBA(Image* outImg, uint w, uint h, const RGBA* rgba);
Image CreateSolidColorImageRGBA(uint w, uint h, const RGBA& color);
Image CreateCheckerboardImageRGBA(uint w, uint h, const RGBA& color1, const RGBA& color2, int checkerSize = -1);

bool LoadImageFromFile(
	const std::filesystem::path& path, 
	Image* outImage);
bool SaveImageToFile(
	const std::filesystem::path& path,
	const Image& img, 
	IMAGE_FORMAT format = IMAGE_FORMAT_RGBA8);

struct aiScene;
struct aiMaterial;
enum aiTextureType;

bool LoadExternalTexture(
	const std::filesystem::path& modelDir,
	const std::string& relPath,
	Image* outImg);
bool LoadEmbeddedTexture(
	const aiScene* scene,
	const std::string& starPath,
	Image* outImg);

bool TryLoadMaterialTexture(
	const aiScene* scene,
	const aiMaterial* mat,
	aiTextureType type,
	const std::filesystem::path& modelDir,
	Image* outImg);
bool TryLoadNormalLike(
	const aiScene* scene,
	const aiMaterial* mat,
	const std::filesystem::path& modelDir,
	Image* outImg);
bool TryLoadMetallic(
	const aiScene* scene,
	const aiMaterial* mat,
	const std::filesystem::path& modelDir,
	Image* outImg);
bool TryLoadRoughness(
	const aiScene* scene,
	const aiMaterial* mat,
	const std::filesystem::path& modelDir,
	Image* outImg);