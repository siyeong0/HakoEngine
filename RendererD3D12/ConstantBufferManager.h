#pragma once
#include "Common/Common.h"

class SimpleConstantBufferPool;

class ConstantBufferManager
{
public:
	ConstantBufferManager() = default;
	~ConstantBufferManager();

	bool Initialize(ID3D12Device* pD3DDevice, int maxNumCBV);
	void Reset();
	SimpleConstantBufferPool* GetConstantBufferPool(EConstantBufferType type);

private:
	SimpleConstantBufferPool* m_ppConstantBufferPool[CONSTANT_BUFFER_TYPE_COUNT] = {};
};