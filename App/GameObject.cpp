#include <Windows.h>
#include <DirectXMath.h>
#include "Common/Common.h"
#include "Interface/IRenderer.h"
#include "Util/LinkedList.h"
#include "Util/VertexUtil.h"
#include "Game.h"

#include "GameObject.h"

GameObject::GameObject()
{
	m_ID = rand() % 100000;

	m_LinkInGame.pItem = this;
	m_LinkInGame.pNext = nullptr;
	m_LinkInGame.pPrv = nullptr;

	m_ScaleMatrix = XMMatrixIdentity();
	m_RotMatrix = XMMatrixIdentity();
	m_TransMatrix = XMMatrixIdentity();
	m_WorldMatrix = XMMatrixIdentity();
}

GameObject::~GameObject()
{
	Cleanup();
}


bool GameObject::Initialize(Game* pGame)
{
	bool bResult = false;
	Game* m_pGame = pGame;
	m_pRenderer = pGame->GetRenderer();

	m_pMeshObj = CreateBoxMeshObject();
	if (m_pMeshObj)
	{
		bResult = true;
	}
	return bResult;
}

void GameObject::Run()
{
	const float MOVE_OFFSET = 0.05f;
	const float SCALE_OFFSET = 0.025f;

	switch (m_ID % 7)
	{
	case 0:
	{
		if (m_MovedOffset.x > 1.0f)
		{
			m_MoveSign = -1.0f;
		}
		else if (m_MovedOffset.x < -1.0f)
		{
			m_MoveSign = 1.0f;
		}
		float move_x = MOVE_OFFSET * m_MoveSign;

		XMFLOAT3 Pos;
		GetPosition(&Pos.x, &Pos.y, &Pos.z);

		Pos.x += move_x;
		m_MovedOffset.x += move_x;

		SetPosition(Pos.x, Pos.y, Pos.z);
	}
	break;
	case 1:
	{
		if (m_MovedOffset.y > 1.0f)
		{
			m_MoveSign = -1.0f;
		}
		else if (m_MovedOffset.y < -1.0f)
		{
			m_MoveSign = 1.0f;
		}
		float move_y = MOVE_OFFSET * m_MoveSign;

		XMFLOAT3 Pos;
		GetPosition(&Pos.x, &Pos.y, &Pos.z);

		Pos.y += move_y;
		m_MovedOffset.y += move_y;

		SetPosition(Pos.x, Pos.y, Pos.z);
	}
	break;
	case 2:
	{
		if (m_MovedOffset.z > 1.0f)
		{
			m_MoveSign = -1.0f;
		}
		else if (m_MovedOffset.z < -1.0f)
		{
			m_MoveSign = 1.0f;
		}
		float move_z = MOVE_OFFSET * m_MoveSign;

		XMFLOAT3 Pos;
		GetPosition(&Pos.x, &Pos.y, &Pos.z);

		Pos.z += move_z;
		m_MovedOffset.z += move_z;

		SetPosition(Pos.x, Pos.y, Pos.z);
	}
	break;
	case 3:
	{
		float rad = GetRotationY();
		rad += 0.01f;
		if (rad >= 2.0f * 3.1415f)
		{
			rad = 0.0f;
		}
		SetRotationY(rad);
	}
	break;
	case 4:
	{
		XMFLOAT3 Scale;
		GetScale(&Scale.x, &Scale.y, &Scale.z);

		if (Scale.x > 1.5f)
		{
			m_ScaleSign = -1.0f;
		}
		else if (Scale.x < 0.5f)
		{
			m_ScaleSign = 1.0f;
		}
		Scale.x += (SCALE_OFFSET * m_ScaleSign);

		SetScale(Scale.x, Scale.y, Scale.z);
	}
	break;
	case 5:
	{
		XMFLOAT3 Scale;
		GetScale(&Scale.x, &Scale.y, &Scale.z);

		if (Scale.y > 1.5f)
		{
			m_ScaleSign = -1.0f;
		}
		else if (Scale.y < 0.5f)
		{
			m_ScaleSign = 1.0f;
		}
		Scale.y += (SCALE_OFFSET * m_ScaleSign);

		SetScale(Scale.x, Scale.y, Scale.z);
	}
	break;
	case 6:
	{
		XMFLOAT3 Scale;
		GetScale(&Scale.x, &Scale.y, &Scale.z);

		if (Scale.z > 1.5f)
		{
			m_ScaleSign = -1.0f;
		}
		else if (Scale.z < 0.5f)
		{
			m_ScaleSign = 1.0f;
		}
		Scale.z += (SCALE_OFFSET * m_ScaleSign);

		SetScale(Scale.x, Scale.y, Scale.z);
	}
	break;
	}
	// per 30FPS or 60 FPS
	if (m_bUpdateTransform)
	{
		updateTransform();
	}
	else
	{
		int a = 0;
	}
}

void GameObject::Render()
{
	if (m_pMeshObj)
	{
		m_pRenderer->RenderMeshObject(m_pMeshObj, &m_WorldMatrix);
	}
}

void GameObject::Cleanup()
{
	if (m_pMeshObj)
	{
		m_pMeshObj->Release();
		m_pMeshObj = nullptr;
	}
}

void GameObject::GetPosition(float* pfOutX, float* pfOutY, float* pfOutZ)
{
	*pfOutX = m_Pos.m128_f32[0];
	*pfOutY = m_Pos.m128_f32[1];
	*pfOutZ = m_Pos.m128_f32[2];
}

void GameObject::GetScale(float* pfOutX, float* pfOutY, float* pfOutZ)
{
	*pfOutX = m_Scale.m128_f32[0];
	*pfOutY = m_Scale.m128_f32[1];
	*pfOutZ = m_Scale.m128_f32[2];
}

float GameObject::GetRotationY()
{
	return m_RotY;
}

void GameObject::SetPosition(float x, float y, float z)
{
	m_Pos.m128_f32[0] = x;
	m_Pos.m128_f32[1] = y;
	m_Pos.m128_f32[2] = z;

	m_TransMatrix = XMMatrixTranslation(x, y, z);

	m_bUpdateTransform = true;
}

void GameObject::SetScale(float x, float y, float z)
{
	m_Scale.m128_f32[0] = x;
	m_Scale.m128_f32[1] = y;
	m_Scale.m128_f32[2] = z;

	m_ScaleMatrix = XMMatrixScaling(x, y, z);

	m_bUpdateTransform = true;
}

void GameObject::SetRotationY(float fRotY)
{
	m_RotY = fRotY;
	m_RotMatrix = XMMatrixRotationY(fRotY);

	m_bUpdateTransform = true;
}

IMeshObject* GameObject::CreateBoxMeshObject()
{
	IMeshObject* pMeshObj = nullptr;

	// create box mesh
	// create vertices and indices
	uint16_t indices[36] = {};
	BasicVertex* vertices = nullptr;
	uint32_t numVertices = CreateBoxMesh(&vertices, indices, (uint32_t)_countof(indices), 0.25f);

	// create BasicMeshObject from Renderer
	pMeshObj = m_pRenderer->CreateBasicMeshObject();

	const WCHAR* wchTexFileNameList[6] =
	{
		L"./Resources/KittyCraft_01.dds",
		L"./Resources/KittyCraft_02.dds",
		L"./Resources/KittyCraft_03.dds",
		L"./Resources/KittyCraft_04.dds",
		L"./Resources/KittyCraft_05.dds",
		L"./Resources/KittyCraft_06.dds"
	};

	// Set meshes to the BasicMeshObject
	pMeshObj->BeginCreateMesh(vertices, numVertices, 6);	// 박스의 6면-1면당 삼각형 2개-인덱스 6개
	for (int i = 0; i < 6; i++)
	{
		pMeshObj->InsertTriGroup(indices + i * 6, 2, wchTexFileNameList[i]);
	}
	pMeshObj->EndCreateMesh();

	// delete vertices and indices
	if (vertices)
	{
		DeleteBoxMesh(vertices);
		vertices = nullptr;
	}
	return pMeshObj;
}

IMeshObject* GameObject::CreateQuadMesh()
{
	IMeshObject* pMeshObj = m_pRenderer->CreateBasicMeshObject();

	// Set meshes to the BasicMeshObject
	BasicVertex vertices[] =
	{
		{ { -0.25f, 0.25f, 0.0f }, { 1.0f, 1.0f, 1.0f, 1.0f }, { 0.0f, 0.0f } },
		{ { 0.25f, 0.25f, 0.0f }, { 1.0f, 1.0f, 1.0f, 1.0f }, { 1.0f, 0.0f } },
		{ { 0.25f, -0.25f, 0.0f }, { 1.0f, 1.0f, 1.0f, 1.0f }, { 1.0f, 1.0f } },
		{ { -0.25f, -0.25f, 0.0f }, { 1.0f, 1.0f, 1.0f, 1.0f }, { 0.0f, 1.0f } },
	};

	uint16_t indices[] =
	{
		0, 1, 2,
		0, 2, 3
	};

	pMeshObj->BeginCreateMesh(vertices, (uint32_t)_countof(vertices), 1);
	pMeshObj->InsertTriGroup(indices, 2, L"tex_06.dds");
	pMeshObj->EndCreateMesh();
	return pMeshObj;
}

void GameObject::updateTransform()
{
	// world matrix = scale x rotation x trasnlation
	m_WorldMatrix = XMMatrixMultiply(m_ScaleMatrix, m_RotMatrix);
	m_WorldMatrix = XMMatrixMultiply(m_WorldMatrix, m_TransMatrix);

	m_bUpdateTransform = false;
}