#pragma once

#include "Common/IndexCreator.h"

class SingleDescriptorAllocator
{
public:
	SingleDescriptorAllocator() = default;
	~SingleDescriptorAllocator() { Cleanup(); }

	bool Initialize(ID3D12Device* pDevice, UINT maxCount, D3D12_DESCRIPTOR_HEAP_FLAGS flags);
	bool AllocDescriptorHandle(D3D12_CPU_DESCRIPTOR_HANDLE* outCPUHandle);
	void FreeDescriptorHandle(D3D12_CPU_DESCRIPTOR_HANDLE descriptorHandle);
	bool Check(D3D12_CPU_DESCRIPTOR_HANDLE descriptorHandle);
	void Cleanup();

	D3D12_GPU_DESCRIPTOR_HANDLE GetGPUHandleFromCPUHandle(D3D12_CPU_DESCRIPTOR_HANDLE cpuHandle);
	ID3D12DescriptorHeap* GetDescriptorHeap() { return m_pHeap; }

private:
	ID3D12Device* m_pD3DDevice = nullptr;
	ID3D12DescriptorHeap* m_pHeap = nullptr;
	CIndexCreator m_IndexCreator;
	UINT m_DescriptorSize = 0;
};

