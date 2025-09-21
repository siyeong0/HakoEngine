#pragma once

struct ConstantBufferContainer
{
	D3D12_CPU_DESCRIPTOR_HANDLE	CBVHandle;
	D3D12_GPU_VIRTUAL_ADDRESS	pGPUMemAddr;
	UINT8*						pSystemMemAddr;
};

class SimpleConstantBufferPool
{
public:
	SimpleConstantBufferPool() = default;
	~SimpleConstantBufferPool() { Cleanup(); }

	bool Initialize(ID3D12Device* pD3DDevice, EConstantBufferType type, UINT sizePerCBV, int maxNumCBV);
	ConstantBufferContainer* Alloc();
	void Reset();
	void Cleanup();

private:
	ConstantBufferContainer* m_pCBContainerList = nullptr;
	EConstantBufferType m_ConstantBufferType = CONSTANT_BUFFER_TYPE_DEFAULT;
	ID3D12DescriptorHeap* m_pCBVHeap = nullptr;
	ID3D12Resource* m_pResource = nullptr;
	uint8_t* m_pSystemMemAddr = nullptr;
	UINT m_SizePerCBV = 0;
	int m_MaxCBVNum = 0;
	int m_AllocatedCBVNum = 0;
};

