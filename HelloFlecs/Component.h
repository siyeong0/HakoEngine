#pragma once

struct Position
{
	float x;
	float y;
	float z;
};

struct Velocity
{
	float x;
	float y;
	float z;
};

struct Force
{
	float x;
	float y;
	float z;
};

struct Rotation
{
	float Pitch;
	float Yaw;
	float Roll;
};

struct Scale
{
	float x;
	float y;
	float z;
};

struct MeshRenderer
{
	IMeshObject* Mesh = nullptr;
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