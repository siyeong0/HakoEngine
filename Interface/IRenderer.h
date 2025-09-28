#pragma once
#include <Windows.h>
#include <combaseapi.h>
#include "Common/Common.h"

#include "IMeshObject.h"
#include "ISpriteObject.h"

interface IRenderer : public IUnknown
{
	virtual bool ENGINECALL Initialize(HWND hWnd, bool bEnableDebugLayer, bool bEnableGBV, bool bUseGpuUploadHeaps, const WCHAR* wchShaderPath) = 0;
	virtual void ENGINECALL BeginRender() = 0;
	virtual void ENGINECALL EndRender() = 0;
	virtual void ENGINECALL Present() = 0;
	virtual void ENGINECALL Cleanup() = 0;

	virtual bool ENGINECALL UpdateWindowSize(uint32_t backBufferWidth, uint32_t backBufferHeight) = 0;

	virtual IMeshObject* ENGINECALL CreateBasicMeshObject() = 0;
	virtual ISprite* ENGINECALL CreateSpriteObject() = 0;
	virtual ISprite* ENGINECALL CreateSpriteObject(const WCHAR* wchTexFileName) = 0;
	virtual ISprite* ENGINECALL CreateSpriteObject(const WCHAR* wchTexFileName, int posX, int posY, int width, int height) = 0;

	virtual void* ENGINECALL CreateTiledTexture(UINT texWidth, UINT texHeight, uint8_t r, uint8_t g, uint8_t b) = 0;
	virtual void* ENGINECALL CreateDynamicTexture(UINT texWidth, UINT texHeight) = 0;
	virtual void* ENGINECALL CreateTextureFromFile(const WCHAR* wchFileName) = 0;
	virtual void ENGINECALL DeleteTexture(void* pTexHandle) = 0;

	virtual void* ENGINECALL CreateFontObject(const WCHAR* wchFontFamilyName, float fontSize) = 0;
	virtual void ENGINECALL DeleteFontObject(void* pFontHandle) = 0;
	virtual bool ENGINECALL WriteTextToBitmap(uint8_t* dstImage, UINT dstWidth, UINT dstHeight, UINT dstPitch, int* outWidth, int* outHeight, void* pFontObjHandle, const WCHAR* wchString, UINT len) = 0;

	virtual void ENGINECALL RenderMeshObject(IMeshObject* pMeshObj, const XMMATRIX* pMatWorld) = 0;
	virtual void ENGINECALL RenderSpriteWithTex(void* pSprObjHandle, int posX, int posY, float scaleX, float scaleY, const RECT* pRect, float z, void* pTexHandle) = 0;
	virtual void ENGINECALL RenderSprite(void* pSprObjHandle, int posX, int posY, float scaleX, float scaleY, float z) = 0;
	virtual void ENGINECALL UpdateTextureWithImage(void* pTexHandle, const BYTE* pSrcBits, UINT srcWidth, UINT srcHeight) = 0;

	virtual void ENGINECALL SetCameraPos(float x, float y, float z) = 0;
	virtual void ENGINECALL SetCameraRot(float yaw, float pitch, float roll) = 0;
	virtual void ENGINECALL MoveCamera(float x, float y, float z) = 0;
	virtual void ENGINECALL GetCameraPos(float* outX, float* outY, float* outZ) = 0;
	virtual int ENGINECALL GetCommandListCount() = 0;
	virtual bool ENGINECALL IsGpuUploadHeapsEnabled() const = 0;
};