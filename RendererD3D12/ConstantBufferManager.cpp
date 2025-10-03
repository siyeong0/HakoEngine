#include "pch.h"

#include "SimpleConstantBufferPool.h"

#include "ConstantBufferManager.h"

ConstantBufferProperty g_pConstBufferPropList[] =
{
	{ CONSTANT_BUFFER_TYPE_PER_FRAME, sizeof(CB_PerFrame) },
	{ CONSTANT_BUFFER_TYPE_MESH, sizeof(CB_MeshObject) },
	{ CONSTANT_BUFFER_TYPE_SPRITE, sizeof(CB_SpriteObject) },
	{ CONSTANT_BUFFER_TYPE_ATMOS_CONSTANTS, sizeof(CB_AtmosConstants) },
	{ CONSTANT_BUFFER_TYPE_RAY_TRACING, sizeof(CB_RayTracing) },
};

ConstantBufferManager::~ConstantBufferManager()
{
	for (int i = 0; i < CONSTANT_BUFFER_TYPE_COUNT; i++)
	{
		if (m_ppConstantBufferPool[i])
		{
			delete m_ppConstantBufferPool[i];
			m_ppConstantBufferPool[i] = nullptr;
		}
	}
}

bool ConstantBufferManager::Initialize(ID3D12Device* pD3DDevice, int maxNumCBV)
{
	for (int i = 0; i < CONSTANT_BUFFER_TYPE_COUNT; i++)
	{
		m_ppConstantBufferPool[i] = new SimpleConstantBufferPool;
		m_ppConstantBufferPool[i]->Initialize(pD3DDevice, (EConstantBufferType)i, static_cast<UINT>(AlignConstantBufferSize(g_pConstBufferPropList[i].Size)), maxNumCBV);
	}
	return true;
}

void ConstantBufferManager::Reset()
{
	for (int i = 0; i < CONSTANT_BUFFER_TYPE_COUNT; i++)
	{
		m_ppConstantBufferPool[i]->Reset();
	}
}

SimpleConstantBufferPool* ConstantBufferManager::GetConstantBufferPool(EConstantBufferType type) const
{
	ASSERT(type < CONSTANT_BUFFER_TYPE_COUNT);
#pragma warning(suppress: 33010)
	return m_ppConstantBufferPool[type];
}


