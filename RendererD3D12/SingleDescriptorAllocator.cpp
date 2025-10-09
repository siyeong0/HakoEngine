#include "pch.h"

#include "SingleDescriptorAllocator.h"

bool SingleDescriptorAllocator::Initialize(ID3D12Device* pDevice, uint maxCount, D3D12_DESCRIPTOR_HEAP_FLAGS flags)
{
	HRESULT hr = S_OK;

	m_pD3DDevice = pDevice;
	m_pD3DDevice->AddRef();

	//D3D12_DESCRIPTOR_HEAP_FLAG_NONE;

	D3D12_DESCRIPTOR_HEAP_DESC commonHeapDesc = {};
	commonHeapDesc.NumDescriptors = maxCount;
	commonHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
	commonHeapDesc.Flags = flags;

	hr = m_pD3DDevice->CreateDescriptorHeap(&commonHeapDesc, IID_PPV_ARGS(&m_pHeap));
	ASSERT(SUCCEEDED(hr), "Failed to create descriptor heap.");

	m_IndexCreator.Initialize(maxCount);

	m_DescriptorSize = m_pD3DDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

	return true;
}

bool SingleDescriptorAllocator::AllocDescriptorHandle(D3D12_CPU_DESCRIPTOR_HANDLE* outCPUHandle)
{
	uint32_t index = m_IndexCreator.Alloc();
	if (index == (uint32_t)-1)
	{
		// all used
		return false;
	}
	CD3DX12_CPU_DESCRIPTOR_HANDLE descriptorHandle(m_pHeap->GetCPUDescriptorHandleForHeapStart(), index, m_DescriptorSize);
	*outCPUHandle = descriptorHandle;
	return true;
}

bool SingleDescriptorAllocator::Check(D3D12_CPU_DESCRIPTOR_HANDLE descriptorHandle)
{
	D3D12_CPU_DESCRIPTOR_HANDLE base = m_pHeap->GetCPUDescriptorHandleForHeapStart();
	if (base.ptr > descriptorHandle.ptr)
	{
		ASSERT(false, "Invalid descriptor handle.");
		return false;
	}
	return true;
}

void SingleDescriptorAllocator::Cleanup()
{
#ifdef _DEBUG
	m_IndexCreator.Check();
#endif
	SAFE_RELEASE(m_pHeap);
	SAFE_RELEASE(m_pD3DDevice);
}

void SingleDescriptorAllocator::FreeDescriptorHandle(D3D12_CPU_DESCRIPTOR_HANDLE descriptorHandle)
{
	D3D12_CPU_DESCRIPTOR_HANDLE base = m_pHeap->GetCPUDescriptorHandleForHeapStart();
	ASSERT(base.ptr <= descriptorHandle.ptr, "Invalid descriptor handle.");
	int index = static_cast<int>((descriptorHandle.ptr - base.ptr) / m_DescriptorSize);
	m_IndexCreator.Free(index);
}

D3D12_GPU_DESCRIPTOR_HANDLE SingleDescriptorAllocator::GetGPUHandleFromCPUHandle(D3D12_CPU_DESCRIPTOR_HANDLE cpuHandle)
{
	D3D12_CPU_DESCRIPTOR_HANDLE base = m_pHeap->GetCPUDescriptorHandleForHeapStart();
	ASSERT(base.ptr <= cpuHandle.ptr, "Invalid descriptor handle.");
	int index = static_cast<int>((cpuHandle.ptr - base.ptr) / m_DescriptorSize);
	CD3DX12_GPU_DESCRIPTOR_HANDLE gpuHandle(m_pHeap->GetGPUDescriptorHandleForHeapStart(), index, m_DescriptorSize);
	return gpuHandle;
}