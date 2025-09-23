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
	IMeshObject* Mesh;
};

struct SpriteRenderer
{
	std::wstring SpriteFileName;
	ISprite* Sprite;
};

struct TextRenderer
{
	ISprite* Sprite;
	std::wstring Text;
	void* pFontObject;
	void* pTextTexHandle;
};