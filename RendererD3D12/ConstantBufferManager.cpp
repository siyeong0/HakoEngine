#include "pch.h"

#include "SimpleConstantBufferPool.h"

#include "ConstantBufferManager.h"

CONSTANT_BUFFER_PROPERTY g_pConstBufferPropList[] =
{
	{ CONSTANT_BUFFER_TYPE_PER_FRAME, sizeof(CONSTANT_BUFFER_PER_FRAME) },
	{ CONSTANT_BUFFER_TYPE_MESH, sizeof(CONSTANT_BUFFER_MESH_OBJECT) },
	{ CONSTANT_BUFFER_TYPE_SPRITE, sizeof(CONSTANT_BUFFER_SPRITE_OBJECT) },
	{ CONSTANT_BUFFER_TYPE_ATMOS_CONSTANTS, sizeof(CONSTANT_BUFFER_ATMOS) },
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
		m_ppConstantBufferPool[i]->Initialize(pD3DDevice, (CONSTANT_BUFFER_TYPE)i, static_cast<uint>(D3DUtil::AlignConstantBufferSize(g_pConstBufferPropList[i].Size)), maxNumCBV);
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

SimpleConstantBufferPool* ConstantBufferManager::GetConstantBufferPool(CONSTANT_BUFFER_TYPE type) const
{
	ASSERT(type < CONSTANT_BUFFER_TYPE_COUNT);
#pragma warning(suppress: 33010)
	return m_ppConstantBufferPool[type];
}


