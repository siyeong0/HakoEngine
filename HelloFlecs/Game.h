#pragma once
#include <vector>

#pragma warning(push)
#pragma warning(disable : 26495 6385 6386)
#include <flecs.h>
#pragma warning(pop)

#include "Common/Common.h"
#include "Interface/IRenderer.h"
#include "Interface/IGeometry.h"
#include "Common/QueryPerfCounter.h"

class GameObject;
class Game
{
public:
	Game() { QCInit(); }
	~Game() { Cleanup(); }

	bool Initialize(HWND hWnd, bool bEnableDebugLayer, bool bEnableGBV, bool bEnableShaderDebug);
	void Run();
	bool Update(uint64_t currTick);
	void Cleanup();

	void OnKeyDown(UINT nChar, UINT uiScanCode);
	void OnKeyUp(UINT nChar, UINT uiScanCode);
	void OnMouseLButtonDown(int x, int y, UINT nFlags);
	void OnMouseLButtonUp(int x, int y, UINT nFlags);
	void OnMouseRButtonDown(int x, int y, UINT nFlags);
	void OnMouseRButtonUp(int x, int y, UINT nFlags);
	void OnMouseMButtonDown(int x, int y, UINT nFlags);
	void OnMouseMButtonUp(int x, int y, UINT nFlags);
	void OnMouseMove(int x, int y, UINT nFlags);
	void OnMouseWheel(int x, int y, int iWheel);
	void OnMouseHWheel(int x, int y, int iWheel);
	bool UpdateWindowSize(uint32_t backBufferWidth, uint32_t backBufferHeight);

	IRenderer* GetRenderer() const { return m_pRenderer; }
	IGeometry* GetGeometry() const { return m_pGeometry; }

private:
	HMODULE m_hRendererDLL = nullptr;
	IRenderer* m_pRenderer = nullptr;
	HWND m_hWnd = nullptr;

	HMODULE m_hGeometryDLL = nullptr;
	IGeometry* m_pGeometry = nullptr;

	bool m_bShiftKeyDown = false;

	float m_CamOffsetX = 0.0f;
	float m_CamOffsetY = 0.0f;
	float m_CamOffsetZ = 0.0f;

	bool m_bCamRotMode = false;
	int m_CurrMouseX = 0;
	int m_CurrMouseY = 0;
	int m_PrevMouseX = 0;
	int m_PrevMouseY = 0;
	int m_MouseXRButtonPressed = 0;
	int m_MouseYRButtonPressed = 0;
	bool m_bMouseLButtonDown = false;
	bool m_bMouseMButtonDown = false;
	bool m_bMouseRButtonDown = false;

	// Game Objects
	flecs::world m_ECSWorld;
	static constexpr UINT MAX_GAME_OBJECTS = 2 << 16;
	std::vector<flecs::entity_t> m_Entities;

	uint64_t m_PrevFrameCheckTick = 0;
	uint64_t m_PrevUpdateTick = 0;
	uint32_t m_FrameCount = 0;
	uint32_t m_FPS = 0;
	uint32_t m_NumCommandLists = 0;
	WCHAR m_wchText[64] = {};

	WCHAR m_wchAppPath[_MAX_PATH] = {};
};