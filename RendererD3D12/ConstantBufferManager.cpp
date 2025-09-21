#include "pch.h"

#include "SimpleConstantBufferPool.h"

#include "ConstantBufferManager.h"

ConstantBufferProperty g_pConstBufferPropList[] =
{
	CONSTANT_BUFFER_TYPE_DEFAULT, sizeof(ConstantBufferDefault),
	CONSTANT_BUFFER_TYPE_SPRITE, sizeof(ConstantBufferSprite)
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
	return TRUE;
}

void ConstantBufferManager::Reset()
{
	for (int i = 0; i < CONSTANT_BUFFER_TYPE_COUNT; i++)
	{
		m_ppConstantBufferPool[i]->Reset();
	}
}

SimpleConstantBufferPool* ConstantBufferManager::GetConstantBufferPool(EConstantBufferType type)
{
	ASSERT(type < CONSTANT_BUFFER_TYPE_COUNT);
#pragma warning(suppress: 33010)
	return m_ppConstantBufferPool[type];
}


