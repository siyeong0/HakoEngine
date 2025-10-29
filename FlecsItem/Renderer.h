#pragma once
#include "../Interface/IMeshObject.h"
#include "../Interface/IProceduralSphereObject.h"
#include "../Interface/ICasperObject.h"
#include "../Interface/ISpriteObject.h"

struct CMeshRenderer
{
	IMeshObject* Mesh = nullptr;
};

struct ProceduralSphereRenderer
{
	IProceduralSphereObject* Sphere = nullptr;
};

struct CasperRenderer
{
	ICasperObject* Casper = nullptr;
};

struct SpriteRenderer
{
	std::wstring SpriteFileName;
	ISprite* Sprite = nullptr;
};

struct TextRenderer
{
	std::wstring Text;
	ISprite* Sprite;
	int Width, Height;
	uint8_t* pImageData;
	void* pFontObject;
	void* pTextTexHandle;

	TextRenderer() : Text(L""), Sprite(nullptr), Width(0), Height(0), pImageData(nullptr), pFontObject(nullptr), pTextTexHandle(nullptr) {}
	TextRenderer(const std::wstring& text) : Text(text), Sprite(nullptr), Width(0), Height(0), pImageData(nullptr), pFontObject(nullptr), pTextTexHandle(nullptr) {}
};