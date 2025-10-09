#pragma once
#include <Windows.h>
#include <DirectXMath.h>

using namespace DirectX;

enum RenderItemType
{
	RENDER_ITEM_TYPE_MESH_OBJ,
	RENDER_ITEM_TYPE_SPRITE
};

struct RenderObjectParam
{
	Matrix4x4 matWorld;
};

struct RenderSpriteParam
{
	int PosX;
	int PosY;
	float ScaleX;
	float ScaleY;
	RECT Rect;
	bool bUseRect;
	float Z;
	void* pTexHandle;
};

struct RenderItem
{
	RenderItemType Type;
	void* pObjHandle;
	union
	{
		RenderObjectParam	MeshObjParam;
		RenderSpriteParam	SpriteParam;
	};
};