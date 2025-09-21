#pragma once
#include "Interface/IRenderer.h"
#include "../Util/LinkedList.h"

class Game;

class GameObject
{
public:
	SORT_LINK	m_LinkInGame;

	GameObject();
	~GameObject();

	bool Initialize(Game* pGame);
	void Run();
	void Render();
	void Cleanup();

	IMeshObject* CreateBoxMeshObject();
	IMeshObject* CreateQuadMesh();

	void	GetPosition(float* pfOutX, float* pfOutY, float* pfOutZ);
	void	GetScale(float* pfOutX, float* pfOutY, float* pfOutZ);
	float	GetRotationY();
	void	SetPosition(float x, float y, float z);
	void	SetScale(float x, float y, float z);
	void	SetRotationY(float fRotY);

private:
	void	updateTransform();

private:
	int m_ID = -1;
	Game* m_pGame = nullptr;
	IRenderer* m_pRenderer = nullptr;
	IMeshObject* m_pMeshObj = nullptr;

	XMVECTOR m_Scale = { 1.0f, 1.0f, 1.0f, 0.0f };
	XMVECTOR m_Pos = {};
	float m_RotY = 0.0f;

	XMMATRIX m_ScaleMatrix = {};
	XMMATRIX m_RotMatrix = {};
	XMMATRIX m_TransMatrix = {};
	XMMATRIX m_WorldMatrix = {};

	float m_HalfLen = 0.0;
	bool m_bUpdateTransform = false;
	bool m_bDeformable = false;
	XMFLOAT3 m_MovedOffset = {};
	float m_MoveSign = 1.0f;
	XMFLOAT3 m_ScaledOffset = {};
	float m_ScaleSign = 1.0f;
};