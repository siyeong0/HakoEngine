#include "pch.h"

#include "D3D12Renderer.h"

#include "SimpleConstantBufferPool.h"

bool SimpleConstantBufferPool::Initialize(ID3D12Device* pD3DDevice, CONSTANT_BUFFER_TYPE type, uint sizePerCBV, int maxNumCBV)
{
	HRESULT hr = S_OK;

	m_ConstantBufferType = type;
	m_MaxCBVNum = maxNumCBV;
	m_SizePerCBV = sizePerCBV;
	uint byteWidth = sizePerCBV * m_MaxCBVNum;

	// Create the constant buffer.
	hr = pD3DDevice->CreateCommittedResource(
		&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD),
		D3D12_HEAP_FLAG_NONE,
		&CD3DX12_RESOURCE_DESC::Buffer(byteWidth),
		D3D12_RESOURCE_STATE_COMMON,
		nullptr,
		IID_PPV_ARGS(&m_pResource));
	ASSERT(SUCCEEDED(hr));


	// create descriptor heap
	D3D12_DESCRIPTOR_HEAP_DESC heapDesc = {};
	heapDesc.NumDescriptors = m_MaxCBVNum;
	heapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
	heapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
	hr = pD3DDevice->CreateDescriptorHeap(&heapDesc, IID_PPV_ARGS(&m_pCBVHeap));
	ASSERT(SUCCEEDED(hr));

	CD3DX12_RANGE readRange(0, 0);		// We do not intend to write from this resource on the CPU.
	m_pResource->Map(0, &readRange, reinterpret_cast<void**>(&m_pSystemMemAddr));

	// Create constant buffer views (one for each frame).
	m_pCBContainerList = new ConstantBufferContainer[m_MaxCBVNum];

	D3D12_CONSTANT_BUFFER_VIEW_DESC cbvDesc = {};
	cbvDesc.BufferLocation = m_pResource->GetGPUVirtualAddress();
	cbvDesc.SizeInBytes = m_SizePerCBV;

	uint8_t* systemMemPtr = m_pSystemMemAddr;
	CD3DX12_CPU_DESCRIPTOR_HANDLE	heapHandle(m_pCBVHeap->GetCPUDescriptorHandleForHeapStart());

	uint descriptorSize = pD3DDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
	for (int i = 0; i < m_MaxCBVNum; i++)
	{
		pD3DDevice->CreateConstantBufferView(&cbvDesc, heapHandle);

		m_pCBContainerList[i].CBVHandle = heapHandle;
		m_pCBContainerList[i].pGPUMemAddr = cbvDesc.BufferLocation;
		m_pCBContainerList[i].pSystemMemAddr = systemMemPtr;

		heapHandle.Offset(1, descriptorSize);
		cbvDesc.BufferLocation += m_SizePerCBV;
		systemMemPtr += m_SizePerCBV;
	}

	return true;
}

ConstantBufferContainer* SimpleConstantBufferPool::Alloc()
{
	ConstantBufferContainer* pCB = nullptr;

	ASSERT(m_AllocatedCBVNum < m_MaxCBVNum);

	pCB = m_pCBContainerList + m_AllocatedCBVNum;
	m_AllocatedCBVNum++;

	return pCB;
}

void SimpleConstantBufferPool::Reset()
{
	m_AllocatedCBVNum = 0;
}

void SimpleConstantBufferPool::Cleanup()
{
	SAFE_DELETE_ARRAY(m_pCBContainerList);
	SAFE_RELEASE(m_pResource);
	SAFE_RELEASE(m_pCBVHeap);
}
